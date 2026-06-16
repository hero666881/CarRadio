#include <QApplication>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QSlider>
#include <QStackedWidget>
#include <QSocketNotifier>
#include <QFileInfo>
#include <QTimer>
#include <QProcess>
#include <QDir>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#define UI_CMD_PIPE      "/tmp/ui_cmd"
#define MPLAYER_CMD_PIPE "/tmp/mplayer_cmd"
#define STATUS_PIPE      "/tmp/carradio_status"
#define MAX_SONGS 128

static char  *song_paths[MAX_SONGS];
static char   song_cat [MAX_SONGS][8];
static int    song_count = 0;
static int    ui_fd = -1, mplayer_fd = -1, status_fd = -1;

static void create_fifo(const char *p) { if(access(p,F_OK)==-1) mkfifo(p,0666); }
static void pipe_write(int fd, const char *cmd) {
    if(fd<0) return;
    char buf[256]; int len=snprintf(buf,sizeof(buf),"%s\n",cmd);
    (void)!write(fd,buf,len);
}
static void scan_dir(const QString &s) {
    DIR *d=opendir(s.toUtf8().constData()); if(!d) return;
    struct dirent *e;
    while((e=readdir(d))&&song_count<MAX_SONGS) {
        const char *n=e->d_name; size_t l=strlen(n);
        if(l>4&&strcasecmp(n+l-4,".mp3")==0) {
            int idx=song_count++;
            song_paths[idx]=strdup((s+"/"+n).toUtf8().constData());
            snprintf(song_cat[idx],sizeof(song_cat[idx]),"%s",s.mid(6).toUtf8().constData());
        }
    } closedir(d);
}
static int cmp(const void *a,const void *b){return strcmp(song_paths[*(const int*)a],song_paths[*(const int*)b]);}
static void build_list() {
    song_count=0; scan_dir("media/ch1"); scan_dir("media/ch2"); scan_dir("media/ch3");
    int *ix=(int*)malloc(song_count*sizeof(int));
    for(int i=0;i<song_count;i++)ix[i]=i;
    qsort(ix,song_count,sizeof(int),cmp);
    char *tp[MAX_SONGS]; char tc[MAX_SONGS][8];
    for(int i=0;i<song_count;i++){tp[i]=song_paths[ix[i]];strcpy(tc[i],song_cat[ix[i]]);}
    for(int i=0;i<song_count;i++){song_paths[i]=tp[i];strcpy(song_cat[i],tc[i]);}
    free(ix);
}
static int getDur(const char *path) {
    struct stat st; if(stat(path,&st)!=0) return 240;
    int dur=(int)(st.st_size*8LL/192000);
    return (dur>330)?330:(dur<60?240:dur);
}
static QString fm(int s){return QString("%1:%2").arg(s/60,2,10,QLatin1Char('0')).arg(s%60,2,10,QLatin1Char('0'));}

class CarRadio : public QWidget {
    Q_OBJECT
public:
    QListWidget *songList;
    QLabel *titleLabel=nullptr, *albumLabel=nullptr, *timeLabel=nullptr, *timeTotal=nullptr, *bottomTitle=nullptr;
    QSlider *progressBar;
    QStackedWidget *pages;
    int currentSongIdx=-1;
    bool isPlaying=false;
    QTimer *fakeTimer;

    CarRadio() {
        setWindowTitle("车载多媒体娱乐终端"); resize(1100,680);
        setObjectName("mainWin");
        QVBoxLayout *outer=new QVBoxLayout(this);
        outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);

        // Top
        QWidget *top=new QWidget(); top->setObjectName("topBar"); top->setFixedHeight(40);
        QHBoxLayout *tl=new QHBoxLayout(top); tl->setContentsMargins(20,0,20,0);
        QLabel *lt=new QLabel("车载多媒体娱乐终端"); lt->setObjectName("topTitle");
        tl->addWidget(lt); tl->addStretch();
        QLabel *ltt=new QLabel("12:36"); ltt->setObjectName("topTime"); tl->addWidget(ltt);
        outer->addWidget(top);

        // Body
        QWidget *body=new QWidget();
        QHBoxLayout *bl=new QHBoxLayout(body);
        bl->setContentsMargins(0,0,0,0); bl->setSpacing(0);

        QWidget *nav=new QWidget(); nav->setObjectName("nav"); nav->setFixedWidth(180);
        QVBoxLayout *nl=new QVBoxLayout(nav); nl->setContentsMargins(10,20,10,20); nl->setSpacing(5);
        struct{const char*l,*i;}ms[]={{"音乐","♪"},{"视频","▶"},{"收音","⊙"},{"蓝牙","β"},{"导航","⟐"},{"设置","⚙"}};
        for(int i=0;i<6;i++){
            QPushButton *b=new QPushButton(QString("%1  %2").arg(ms[i].i).arg(ms[i].l));
            b->setProperty("pageIdx",i); b->setObjectName(i==0?"navBtnActive":"navBtn");
            b->setFixedHeight(48);
            connect(b,&QPushButton::clicked,[this,b,i]{
                pages->setCurrentIndex(i);
                QList<QPushButton*> bs=findChildren<QPushButton*>();
                for(auto x:bs){QString o=x->objectName();if(o=="navBtn"||o=="navBtnActive")x->setObjectName("navBtn");}
                b->setObjectName("navBtnActive"); b->style()->unpolish(b); b->style()->polish(b);
            });
            nl->addWidget(b);
        }
        nl->addStretch(); bl->addWidget(nav);
        QWidget *s1=new QWidget(); s1->setFixedWidth(2); s1->setObjectName("sep"); bl->addWidget(s1);

        pages=new QStackedWidget();
        pages->addWidget(buildMusicPage());
        pages->addWidget(buildPlace("视频"));
        pages->addWidget(buildPlace("收音"));
        pages->addWidget(buildPlace("蓝牙"));
        pages->addWidget(buildNaviPage());
        pages->addWidget(buildPlace("设置"));
        bl->addWidget(pages,1);

        QWidget *s2=new QWidget(); s2->setFixedWidth(2); s2->setObjectName("sep"); bl->addWidget(s2);

        QWidget *rp=new QWidget(); rp->setObjectName("albumPanel"); rp->setFixedWidth(260);
        QVBoxLayout *rl=new QVBoxLayout(rp); rl->setContentsMargins(20,30,20,30);
        albumLabel=new QLabel("♫"); albumLabel->setObjectName("albumArt"); albumLabel->setAlignment(Qt::AlignCenter);
        rl->addWidget(albumLabel); rl->addStretch();
QLabel *lb132=new QLabel("当前播放"); lb132->setObjectName("nowTitle"); rl->addWidget(lb132);
        titleLabel=new QLabel("请选择歌曲..."); titleLabel->setObjectName("songTitle"); titleLabel->setWordWrap(true);
        rl->addWidget(titleLabel);
        bl->addWidget(rp);
        outer->addWidget(body);

        // Bottom
        QWidget *bot=new QWidget(); bot->setObjectName("bottomBar"); bot->setFixedHeight(100);
QVBoxLayout *botV=new QVBoxLayout(bot); botV->setContentsMargins(20,4,20,4);
QHBoxLayout *cRow=new QHBoxLayout();
bottomTitle=new QLabel("请选择歌曲..."); bottomTitle->setObjectName("bottomTitle"); cRow->addWidget(bottomTitle);
cRow->addStretch();
QPushButton*ctrls[]={
    new QPushButton("⏮"),new QPushButton("⏯"),new QPushButton("⏭"),
    new QPushButton("🔊-"),new QPushButton("🔊+"),new QPushButton("🔇")
};
ctrls[0]->setObjectName("ctrlBtn");ctrls[0]->setFixedSize(64,48);
ctrls[1]->setObjectName("ctrlBtn");ctrls[1]->setFixedSize(72,48);
ctrls[2]->setObjectName("ctrlBtn");ctrls[2]->setFixedSize(64,48);
for(int i=3;i<6;i++){ctrls[i]->setObjectName("ctrlBtn");ctrls[i]->setFixedSize(40,40);}
cRow->addWidget(ctrls[0]);cRow->addWidget(ctrls[1]);cRow->addWidget(ctrls[2]);
cRow->addSpacing(20);cRow->addWidget(ctrls[3]);cRow->addWidget(ctrls[4]);cRow->addWidget(ctrls[5]);
botV->addLayout(cRow);
connect(ctrls[0],&QPushButton::clicked,[this]{if(currentSongIdx>0)playSong(currentSongIdx-1);});
connect(ctrls[1],&QPushButton::clicked,[this]{pipe_write(mplayer_fd,"pause");isPlaying=!isPlaying;if(isPlaying)fakeTimer->start(100);else fakeTimer->stop();});
connect(ctrls[2],&QPushButton::clicked,[]{pipe_write(ui_fd,"next");});
connect(ctrls[3],&QPushButton::clicked,[]{pipe_write(mplayer_fd,"volume -10 0");});
connect(ctrls[4],&QPushButton::clicked,[]{pipe_write(mplayer_fd,"volume 10 0");});
connect(ctrls[5],&QPushButton::clicked,[]{pipe_write(mplayer_fd,"mute");});
QWidget *pw=new QWidget();pw->setMaximumWidth(440);
QVBoxLayout *pl=new QVBoxLayout(pw);pl->setContentsMargins(0,0,0,0);
QHBoxLayout *tml=new QHBoxLayout();
timeLabel=new QLabel("00:00");timeLabel->setObjectName("timeLbl");
timeTotal=new QLabel("00:00");timeTotal->setObjectName("timeLbl");
tml->addWidget(timeLabel);tml->addStretch();tml->addWidget(timeTotal);
pl->addLayout(tml);
progressBar=new QSlider(Qt::Horizontal);progressBar->setObjectName("progBar");progressBar->setRange(0,1000);
connect(progressBar,&QSlider::sliderReleased,this,&CarRadio::onProgressSeek);
pl->addWidget(progressBar);
QHBoxLayout *prow=new QHBoxLayout();prow->addStretch();prow->addWidget(pw);prow->addStretch();
botV->addLayout(prow);
outer->addWidget(bot);

        setStyleSheet(
            "#mainWin{background-color:#0a0a0f;color:#ccc;}"
            "#topBar{background:#111118;border-bottom:1px solid #1e1e2a;}"
            "#topTitle{color:#ff9900;font-size:16px;font-weight:bold;}"
            "#topTime{color:#888;font-size:13px;}"
            "#nav{background:#0e0e16;}"
            "#navBtn{background:transparent;color:#888;border:none;text-align:left;padding-left:20px;font-size:15px;border-radius:6px;}"
            "#navBtn:hover{background:#1a1a2a;color:#ccc;}"
            "#navBtnActive{background:#1a1a30;color:#ff9900;border:none;text-align:left;padding-left:20px;font-size:15px;border-radius:6px;border-left:3px solid #ff9900;}"
            "#sep{background:#1a1a28;}"
            "#catBtn{background:#151520;color:#aaa;border:1px solid #222;padding:8px 16px;font-size:13px;border-radius:18px;}"
            "#catBtn:hover{background:#ff9900;color:#000;}"
            "#songList{background:transparent;color:#ccc;border:none;font-size:13px;}"
            "#songList::item{padding:7px 12px;border-bottom:1px solid #151520;}"
            "#songList::item:hover{background:#1a1a28;}"
            "#songList::item:selected{background:#1a1a30;color:#ff9900;}"
            "#albumPanel{background:#0e0e16;}"
            "#albumArt{color:#ff9900;font-size:80px;}"
            "#nowTitle{color:#666;font-size:11px;}"
            "#songTitle{color:#ccc;font-size:15px;}"
            "#bottomBar{background:#111118;border-top:1px solid #1e1e2a;}"
            "#timeLbl{color:#888;font-size:11px;}"
            "#progBar::groove:horizontal{height:4px;background:#222;border-radius:2px;}"
            "#progBar::handle:horizontal{width:12px;height:12px;margin:-4px 0;background:#ff9900;border-radius:6px;}"
            "#progBar::sub-page:horizontal{background:#ff9900;border-radius:2px;}"
            "#ctrlBtn{background:transparent;color:#888;border:none;font-size:20px;}"
            "#ctrlBtn:hover{color:#ff9900;}"
            "#statusLbl{color:#555;font-size:11px;}"
            "#placeholderLbl{color:#555;font-size:28px;}"
        );

        connectPipes();loadCategory("ch1");
        fakeTimer=new QTimer(this); connect(fakeTimer,&QTimer::timeout,this,&CarRadio::tickProgress);
    }

    QWidget*buildMusicPage(){
        QWidget *w=new QWidget(); QVBoxLayout *vl=new QVBoxLayout(w);
        vl->setContentsMargins(20,15,20,15); vl->setSpacing(10);
        QHBoxLayout *cr=new QHBoxLayout();
        QLabel *m=new QLabel("音乐分类");m->setObjectName("nowTitle");cr->addWidget(m);
        struct{const char*c,*l;}cs[]={{"ch1","曲库一"},{"ch2","曲库二"},{"ch3","曲库三"}};
        for(auto &c:cs){QPushButton *b=new QPushButton(c.l);b->setObjectName("catBtn");QString cat(c.c);connect(b,&QPushButton::clicked,[this,cat]{loadCategory(cat);});cr->addWidget(b);}
        cr->addStretch(); vl->addLayout(cr);
        songList=new QListWidget(); songList->setObjectName("songList");
        connect(songList,&QListWidget::itemClicked,[this](QListWidgetItem *i){int idx=i->data(Qt::UserRole).toInt();if(idx>=0&&idx<song_count)playSong(idx);});
        vl->addWidget(songList);
        return w;
    }
    QWidget*buildPlace(const QString &n){
        QWidget *w=new QWidget(); QVBoxLayout *vl=new QVBoxLayout(w);
        QLabel *l=new QLabel(n+" — 功能开发中"); l->setObjectName("placeholderLbl"); l->setAlignment(Qt::AlignCenter);
        vl->addWidget(l); return w;
    }
    QWidget*buildNaviPage(){
        QWidget *w=new QWidget(); QVBoxLayout *vl=new QVBoxLayout(w);
        vl->setContentsMargins(20,15,20,15);
        QLabel *n=new QLabel("Linux 导航系统");n->setObjectName("nowTitle");vl->addWidget(n);
        QPushButton *b=new QPushButton("启动导航");b->setObjectName("catBtn");b->setFixedWidth(150);
        connect(b,&QPushButton::clicked,[]{
            QProcess::startDetached("python3",{"-m","http.server","8080","--directory",QDir::homePath()+"/emb260316/UNIX/CarRadio/navi"});
            QProcess::startDetached("firefox",{"http://localhost:8080"});
        });
        vl->addWidget(b);
        QLabel *h=new QLabel("点击按钮启动 Firefox 导航页面");h->setObjectName("statusLbl");vl->addWidget(h);
        vl->addStretch(); return w;
    }

    void connectPipes(){
        create_fifo(UI_CMD_PIPE);create_fifo(MPLAYER_CMD_PIPE);create_fifo(STATUS_PIPE);
        ui_fd=open(UI_CMD_PIPE,O_RDWR);mplayer_fd=open(MPLAYER_CMD_PIPE,O_RDWR);status_fd=open(STATUS_PIPE,O_RDONLY|O_NONBLOCK);
        if(status_fd>=0){QSocketNotifier *n=new QSocketNotifier(status_fd,QSocketNotifier::Read,this);connect(n,&QSocketNotifier::activated,this,&CarRadio::onStatus);}
    }
    void loadCategory(const QString &cat){
        songList->clear();
        for(int i=0;i<song_count;i++){if(cat!=QString(song_cat[i]))continue;QFileInfo fi(song_paths[i]);QListWidgetItem *it=new QListWidgetItem(QString("#%1  %2").arg(i+1).arg(fi.fileName()));it->setData(Qt::UserRole,i);songList->addItem(it);}
    }
    void playSong(int idx){
        currentSongIdx=idx; QFileInfo fi(song_paths[idx]); titleLabel->setText(fi.fileName());if(bottomTitle)bottomTitle->setText(fi.fileName());
        char cmd[32];snprintf(cmd,sizeof(cmd),"play:%d",idx+1);pipe_write(ui_fd,cmd);
        isPlaying=true;int dur=getDur(song_paths[idx]);fakeTimer->start(100);progressBar->setRange(0,dur*10);progressBar->setValue(0);
        timeLabel->setText("00:00");timeTotal->setText(fm(getDur(song_paths[idx])));
        albumLabel->setText("♫");
    }
    void tickProgress(){
        if(!isPlaying)return;
        int v=progressBar->value()+1;if(v>1000){v=0;fakeTimer->stop();isPlaying=false;}
        progressBar->setValue(v);
        int sec=v*getDur(song_paths[currentSongIdx])/1000;timeLabel->setText(fm(sec));
    }
    void onProgressSeek(){
        if(currentSongIdx<0)return;
        int v=progressBar->value();
        int sec=v/10;char cmd[64];snprintf(cmd,sizeof(cmd),"seek %d 2",sec);
        pipe_write(mplayer_fd,cmd);timeLabel->setText(fm(sec));
    }

private slots:
    void onStatus(){
        char buf[256];int n=read(status_fd,buf,sizeof(buf)-1);
        if(n<=0) return;\
        buf[n]=0;
        for(int i=0;i<n;i++)if(buf[i]=='\n'){buf[i]=0;break;}
        if(strncmp(buf,"PLAYING:",8)==0){int num=atoi(buf+8);if(num>0&&num<=song_count){QFileInfo fi(song_paths[num-1]);titleLabel->setText(fi.fileName());if(bottomTitle)bottomTitle->setText(fi.fileName());currentSongIdx=num-1;isPlaying=true;fakeTimer->start(200);progressBar->setValue(0);timeLabel->setText("00:00");timeTotal->setText(fm(getDur(song_paths[num-1])));albumLabel->setText("♫");}}
        else if(strncmp(buf,"DOWNLOADING:",12)==0)albumLabel->setText("↓");
        else if(strcmp(buf,"IDLE")==0){isPlaying=false;fakeTimer->stop();}
        else if(strcmp(buf,"QUIT")==0){isPlaying=false;fakeTimer->stop();}
    }
};

int main(int argc, char *argv[]) {
    chdir("../"); build_list();
    printf("[Qt-UI] 扫描完成, 共 %d 首歌曲\n", song_count);
    QApplication app(argc, argv); CarRadio w; w.show(); return app.exec();
}
#include "main.moc"
