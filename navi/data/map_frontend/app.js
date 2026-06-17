const linuxIP = "192.168.75.129";

/* ================= 地图初始化 ================= */

var map = L.map('map').setView([0, 0], 15);

/* 保留原来的地图 */

L.tileLayer(
'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
{
    attribution: '© OpenStreetMap'
}
).addTo(map);


/* ================= 定位小车 ================= */

var marker = L.marker(
[0,0],
{
    icon: L.divIcon({

        html:'🚗',

        className:'',

        iconSize:[48,48]

    })
}
).addTo(map);


/* ================= 轨迹线 ================= */

var route = L.polyline(
[],
{
    color:'red',

    weight:5
}
).addTo(map);


let first = true;


/* ================= 页面元素 ================= */

const ui = {

    lat:document.getElementById("lat"),

    lng:document.getElementById("lng"),

    distance:document.getElementById("distance"),

    count:document.getElementById("count"),

    time:document.getElementById("time")

};


/* ================= 更新位置 ================= */

async function updateLocation()
{

navigator.geolocation.getCurrentPosition(

async function(pos)
{

try
{

    let lat = pos.coords.latitude;

    let lng = pos.coords.longitude;


    /* 第一次定位 */

    if(first)
    {
        map.setView([lat,lng],18);

        first = false;
    }


    /* 更新小车 */

    marker.setLatLng([lat,lng]);


    /* 更新页面 */

    ui.lat.innerHTML =

    "🛰 纬度<br>" +

    lat.toFixed(6);


    ui.lng.innerHTML =

    "🛰 经度<br>" +

    lng.toFixed(6);


    /* 上传 Linux */

    await fetch(

    `http://${linuxIP}:8080/gps`,

    {

        method:"POST",

        body:`${lat} ${lng}`

    });


    /* 获取轨迹 */

    let res = await fetch(

    `http://${linuxIP}:8080/tracks`

    );


    let data = await res.json();


    /* 更新距离 */

    ui.distance.innerHTML =

    "📏 距离<br>" +

    data.distance.toFixed(2) +

    " m";


    /* 更新轨迹 */

    let points = data.points.map(

    p => [p.lat,p.lng]

    );


    route.setLatLngs(points);


    /* 更新轨迹点数量 */

    ui.count.innerHTML =

    "📍 轨迹点<br>" +

    points.length;


    /* 更新时间 */

    ui.time.innerHTML =

    "🕒 时间<br>" +

    new Date().toLocaleTimeString();

}
catch(err)
{

    console.log(err);

}

},

function(err)
{

    console.log("定位失败：",err);

}

);

}


/* ================= 复位系统 ================= */

async function resetSystem()
{

try
{

    await fetch(

    `http://${linuxIP}:8080/reset`

    );

}
catch(e)
{

    console.log(e);

}


route.setLatLngs([]);


ui.distance.innerHTML =

"📏 距离<br>0 m";


ui.count.innerHTML =

"📍 轨迹点<br>0";


first = true;


/* 地图恢复 */

map.setView([0,0],2);

}


/* ================= 顶部时钟 ================= */

setInterval(()=>{

let now = new Date();

let clock = document.getElementById("clock");

if(clock)
{

clock.innerHTML =

"🕒 " +

now.toLocaleTimeString();

}

},1000);


/* ================= 启动 ================= */

updateLocation();

setInterval(

updateLocation,

3000

);