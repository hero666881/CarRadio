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
    char cmd[512];
    char buf[128];
    FILE *fp;
    int duration = 180;
    
    // 用 mplayer 获取时长
    snprintf(cmd, sizeof(cmd), 
             "mplayer -vo null -ao null -identify -frames 0 \"%s\" 2>/dev/null | grep ID_LENGTH | cut -d= -f2",
             path);
    
    fp = popen(cmd, "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            double dur = strtod(buf, NULL);
            if (dur > 0) {
                duration = (int)(dur + 0.5);
             //   printf("[Mplayer] 时长: %d 秒\n", duration);
            }
        }
        pclose(fp);
    }
    
    if (duration == 180) {
        struct stat st;
        if (stat(path, &st) == 0) {
            duration = (int)(st.st_size * 8LL / 192000);
        }
    }
    
    if (duration < 5) duration = 5;
    return duration;
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
       	albumLabel = new QLabel("↓"); albumLabel->setObjectName("albumArt"); albumLabel->setAlignment(Qt::AlignCenter);
        rl->addWidget(albumLabel); rl->addStretch();
        QLabel *lb132=new QLabel("当前播放"); lb132->setObjectName("nowTitle"); rl->addWidget(lb132);
        titleLabel=new QLabel("请选择歌曲..."); titleLabel->setObjectName("songTitle"); titleLabel->setWordWrap(true);
        rl->addWidget(titleLabel);
        bl->addWidget(rp);
        outer->addWidget(body);

		// Bottom
		QWidget *bot = new QWidget(); 
		bot->setObjectName("bottomBar"); 
		bot->setFixedHeight(120);

		QVBoxLayout *botV = new QVBoxLayout(bot);
		botV->setContentsMargins(20, 4, 20, 4);
		botV->setSpacing(4);

		// 第一行：歌曲标题
		QHBoxLayout *titleRow = new QHBoxLayout();
		bottomTitle = new QLabel("请选择歌曲...");
		bottomTitle->setObjectName("bottomTitle");
		titleRow->addWidget(bottomTitle);
		titleRow->addStretch();
		botV->addLayout(titleRow);

		// 第二行：进度条 + 时间
		QWidget *pw = new QWidget();
		pw->setMaximumWidth(500);
		QVBoxLayout *pl = new QVBoxLayout(pw);
		pl->setContentsMargins(0, 0, 0, 0);

		QHBoxLayout *tml = new QHBoxLayout();
		timeLabel = new QLabel("00:00");
		timeLabel->setObjectName("timeLbl");
		timeTotal = new QLabel("00:00");
		timeTotal->setObjectName("timeLbl");
		tml->addWidget(timeLabel);
		tml->addStretch();
		tml->addWidget(timeTotal);
		pl->addLayout(tml);

		progressBar = new QSlider(Qt::Horizontal);
		progressBar->setObjectName("progBar");
		progressBar->setRange(0, 1000);
		connect(progressBar, &QSlider::sliderReleased, this, &CarRadio::onProgressSeek);
		pl->addWidget(progressBar);

		QHBoxLayout *prow = new QHBoxLayout();
		prow->addStretch();
		prow->addWidget(pw);
		prow->addStretch();
		botV->addLayout(prow);

		// 第三行：控制按钮（上一首、播放/暂停、下一首、音量）
		QHBoxLayout *ctrlRow = new QHBoxLayout();
		ctrlRow->setSpacing(15);

		// 创建控制按钮
		QPushButton *prevBtn = new QPushButton("⏮");
		QPushButton *playBtn = new QPushButton("⏯");
		QPushButton *nextBtn = new QPushButton("⏭");
		QPushButton *volDownBtn = new QPushButton("🔊-");
		QPushButton *volUpBtn = new QPushButton("🔊+");
		QPushButton *muteBtn = new QPushButton("🔇");

		// 设置样式
		prevBtn->setObjectName("ctrlBtn");
		playBtn->setObjectName("ctrlBtn");
		nextBtn->setObjectName("ctrlBtn");
		volDownBtn->setObjectName("ctrlBtn");
		volUpBtn->setObjectName("ctrlBtn");
		muteBtn->setObjectName("ctrlBtn");

		// 设置大小
		prevBtn->setFixedSize(60, 42);
		playBtn->setFixedSize(70, 42);
		nextBtn->setFixedSize(60, 42);
		volDownBtn->setFixedSize(48, 38);
		volUpBtn->setFixedSize(48, 38);
		muteBtn->setFixedSize(48, 38);

		// 居中布局 - 先加弹性空间，然后按钮，再加弹性空间
		ctrlRow->addStretch();
		ctrlRow->addWidget(prevBtn);
		ctrlRow->addWidget(playBtn);
		ctrlRow->addWidget(nextBtn);
		ctrlRow->addSpacing(30);  // 播放控制和音量控制之间的间距
		ctrlRow->addWidget(volDownBtn);
		ctrlRow->addWidget(volUpBtn);
		ctrlRow->addWidget(muteBtn);
		ctrlRow->addStretch();

		botV->addLayout(ctrlRow);

		outer->addWidget(bot);

// 连接信号
connect(prevBtn, &QPushButton::clicked, [this]{ 
    if(currentSongIdx > 0) {
        playSong(currentSongIdx - 1);
    } else {
        playSong(song_count - 1);  // 如果是第一首，跳到最后一首
    }
   // pipe_write(ui_fd, "next");
});

connect(playBtn, &QPushButton::clicked, [this, playBtn]{ 
    pipe_write(mplayer_fd, "pause");
    isPlaying = !isPlaying;
    if(isPlaying) {
        fakeTimer->start(1000);
        playBtn->setText("⏸");
    } else {
        fakeTimer->stop();
        playBtn->setText("⏯");
    }
});

connect(nextBtn, &QPushButton::clicked, [this]{ 
    if(currentSongIdx < song_count - 1) {
        playSong(currentSongIdx + 1);
    } else {
        playSong(0);  // 如果是最后一首，跳到第一首
    }
  // pipe_write(ui_fd, "next");
});

connect(volDownBtn, &QPushButton::clicked, []{ 
    pipe_write(mplayer_fd, "volume -10 0"); 
});

connect(volUpBtn, &QPushButton::clicked, []{ 
    pipe_write(mplayer_fd, "volume 10 0"); 
});

connect(muteBtn, &QPushButton::clicked, []{ 
    pipe_write(mplayer_fd, "mute"); 
});
   

        setStyleSheet(
            "#mainWin{background-color:#0a0a0f;color:#ccc;font-size:14px;}"
            "#topBar{background:#111118;border-bottom:1px solid #1e1e2a;}"
            "#topTitle{color:#ff9900;font-size:20px;font-weight:bold;}"
            "#topTime{color:#888;font-size:16px;}"
            "#nav{background:#0e0e16;}"
            "#navBtn{background:transparent;color:#888;border:none;text-align:left;padding-left:20px;font-size:18px;border-radius:6px;}"
            "#navBtn:hover{background:#1a1a2a;color:#ccc;}"
            "#navBtnActive{background:#1a1a30;color:#ff9900;border:none;text-align:left;padding-left:20px;font-size:18px;border-radius:6px;border-left:3px solid #ff9900;}"
            "#sep{background:#1a1a28;}"
            "#catBtn{background:#151520;color:#aaa;border:1px solid #222;padding:10px 20px;font-size:16px;border-radius:18px;}"
            "#catBtn:hover{background:#ff9900;color:#000;}"
            "#songList{background:transparent;color:#ccc;border:none;font-size:16px;}"
            "#songList::item{padding:10px 15px;border-bottom:1px solid #151520;}"
            "#songList::item:hover{background:#1a1a28;}"
            "#songList::item:selected{background:#1a1a30;color:#ff9900;}"
            "#albumPanel{background:#0e0e16;}"
            "#albumArt{color:#ff9900;font-size:100px;}"
            "#nowTitle{color:#666;font-size:14px;}"
            "#songTitle{color:#ccc;font-size:18px;}"
            "#bottomBar{background:#111118;border-top:1px solid #1e1e2a;}"
            "#timeLbl{color:#888;font-size:14px;}"
            "#progBar::groove:horizontal{height:6px;background:#222;border-radius:3px;}"
            "#progBar::handle:horizontal{width:16px;height:16px;margin:-5px 0;background:#ff9900;border-radius:8px;}"
            "#progBar::sub-page:horizontal{background:#ff9900;border-radius:3px;}"
            "#ctrlBtn{background:transparent;color:#888;border:none;font-size:24px;}"
            "#ctrlBtn:hover{color:#ff9900;}"
            "#statusLbl{color:#555;font-size:14px;}"
            "#placeholderLbl{color:#555;font-size:32px;}"
        );

        connectPipes();loadCategory("ch1");
        fakeTimer=new QTimer(this); connect(fakeTimer,&QTimer::timeout,this,&CarRadio::tickProgress);
    }

    QWidget* buildMusicPage(){
        QWidget *w=new QWidget(); QVBoxLayout *vl=new QVBoxLayout(w);
        vl->setContentsMargins(20,15,20,15); vl->setSpacing(10);
        QHBoxLayout *cr=new QHBoxLayout();
        QLabel *m=new QLabel("音乐分类");m->setObjectName("nowTitle");cr->addWidget(m);
        struct{const char*c,*l;}cs[]={{"ch1","流行音乐"},{"ch2","欧美音乐"},{"ch3","摇滚音乐"}};
        for(auto &c:cs){QPushButton *b=new QPushButton(c.l);b->setObjectName("catBtn");QString cat(c.c);connect(b,&QPushButton::clicked,[this,cat]{loadCategory(cat);});cr->addWidget(b);}
        cr->addStretch(); vl->addLayout(cr);
        songList=new QListWidget(); songList->setObjectName("songList");
        connect(songList,&QListWidget::itemClicked,[this](QListWidgetItem *i){int idx=i->data(Qt::UserRole).toInt();if(idx>=0&&idx<song_count)playSong(idx);});
        vl->addWidget(songList);
        return w;
    }
    QWidget* buildPlace(const QString &n){
        QWidget *w=new QWidget(); QVBoxLayout *vl=new QVBoxLayout(w);
        QLabel *l=new QLabel(n+" — 功能开发中"); l->setObjectName("placeholderLbl"); l->setAlignment(Qt::AlignCenter);
        vl->addWidget(l); return w;
    }
    QWidget* buildNaviPage(){
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
    
    // 新增高亮方法
    void highlightCurrentSong() {
        if (currentSongIdx < 0 || currentSongIdx >= song_count) return;
        songList->clearSelection();
        for (int i = 0; i < songList->count(); i++) {
            QListWidgetItem *item = songList->item(i);
            if (item->data(Qt::UserRole).toInt() == currentSongIdx) {
                songList->setCurrentItem(item);
                songList->scrollToItem(item, QAbstractItemView::PositionAtCenter);
                break;
            }
        }
    }
    void playSong(int idx) {
    if (idx < 0 || idx >= song_count) return;
    
    currentSongIdx = idx;
	albumLabel->setText("♫"); 
    QFileInfo fi(song_paths[idx]);
    titleLabel->setText(fi.fileName());
    if(bottomTitle) bottomTitle->setText(fi.fileName());
    
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "play:%d", idx + 1);
    pipe_write(ui_fd, cmd);
    
    isPlaying = true;
    int dur = getDur(song_paths[idx]);
    
    // ⭐ 改这里：进度条范围 0 到 总时长（秒）
    progressBar->setRange(0, dur);
    progressBar->setValue(0);
    timeLabel->setText("00:00");
    timeTotal->setText(fm(dur));
    albumLabel->setText("♫");
    
    fakeTimer->start(1000);
    highlightCurrentSong();
	}

	void tickProgress() {
	 albumLabel->setText("♫");   
	 if(!isPlaying || currentSongIdx < 0 || currentSongIdx >= song_count) return;
    int current_val = progressBar->value();
    int max_val = progressBar->maximum();  // 总时长（秒）
    
    if (current_val >= max_val) {
        fakeTimer->stop();
        isPlaying = false;
        return;
    }
    
    // 每次增加 1 秒
    int new_val = current_val + 1;
    progressBar->setValue(new_val > max_val ? max_val : new_val);
    
    // 显示分秒格式
    timeLabel->setText(fm(new_val));
	}
	void onProgressSeek() {
    if(currentSongIdx < 0 || currentSongIdx >= song_count) return;
    
    // ⭐ 进度条的值就是秒数
    int target_sec = progressBar->value();
    
 //   printf("[Qt-UI] 拖动进度条到 %d 秒\n", target_sec);
   // fflush(stdout);
    
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "seek %d 2", target_sec);
    pipe_write(mplayer_fd, cmd);
    
    timeLabel->setText(fm(target_sec));
}
private slots:
    void onStatus(){
        char buf[256];int n=read(status_fd,buf,sizeof(buf)-1);
        if(n<=0) return;
        buf[n]=0;
        for(int i=0;i<n;i++)if(buf[i]=='\n'){buf[i]=0;break;}
        if(strncmp(buf,"PLAYING:",8)==0){
            int num=atoi(buf+8);
            if(num>0&&num<=song_count){
                QFileInfo fi(song_paths[num-1]);
                titleLabel->setText(fi.fileName());
                if(bottomTitle)bottomTitle->setText(fi.fileName());
                currentSongIdx=num-1;
                isPlaying=true;
				int dur = getDur(song_paths[num - 1]);
				progressBar->setRange(0, dur);
				progressBar->setValue(0);
				timeLabel->setText("00:00");
				timeTotal->setText(fm(dur));
				albumLabel->setText("♫");

				fakeTimer->start(1000);
				highlightCurrentSong();

			}
		}
        else if(strncmp(buf,"DOWNLOADING:",12)==0) {
		if (albumLabel->text() != "♫") {
       		 albumLabel->setText("↓");
    		}
		}
        else if(strcmp(buf,"IDLE")==0){
            isPlaying=false;
            fakeTimer->stop();
            // 自动切换到下一首
            if (currentSongIdx >= 0 && currentSongIdx < song_count - 1) {
                playSong(currentSongIdx + 1);
                pipe_write(ui_fd, "next");
            } else if (currentSongIdx == song_count - 1) {
                playSong(0);
                pipe_write(ui_fd, "next");
            }
        }
        else if(strcmp(buf,"NEXT_SONG")==0){
            // 由client触发的切歌信号
            if (currentSongIdx >= 0 && currentSongIdx < song_count - 1) {
                playSong(currentSongIdx + 1);
            } else if (currentSongIdx == song_count - 1) {
                playSong(0);
            }
        }
        else if(strcmp(buf,"QUIT")==0){
            isPlaying=false;
            fakeTimer->stop();
        }
    }
};

int main(int argc, char *argv[]) {
    chdir("../"); build_list();
    printf("[Qt-UI] 扫描完成, 共 %d 首歌曲\n", song_count);
    QApplication app(argc, argv); CarRadio w; w.show(); return app.exec();
}
#include "main.moc"
