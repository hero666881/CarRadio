const linuxIP = "192.168.75.129";

var map = L.map('map').setView([0, 0], 15);

L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png').addTo(map);

var marker = L.marker([0, 0]).addTo(map);

var route = L.polyline([], {
    color: 'red',
    weight: 5
}).addTo(map);

let first = true;

const ui = {
    lat: document.getElementById("lat"),
    lng: document.getElementById("lng"),
    distance: document.getElementById("distance"),
    count: document.getElementById("count"),
    time: document.getElementById("time")
};

async function updateLocation()
{
    navigator.geolocation.getCurrentPosition(async function(pos)
    {
        let lat = pos.coords.latitude;
        let lng = pos.coords.longitude;

        if (first)
        {
            map.setView([lat, lng], 18);
            first = false;
        }

        marker.setLatLng([lat, lng]);

        ui.lat.innerHTML = "🛰纬度<br>" + lat;
        ui.lng.innerHTML = "🛰经度<br>" + lng;

        await fetch(`http://${linuxIP}:8080/gps`, {
            method: "POST",
            body: `${lat} ${lng}`
        });

        let res = await fetch(`http://${linuxIP}:8080/tracks`);
        let data = await res.json();

        // ⭐ Linux距离
        ui.distance.innerHTML =
            "📏距离<br>" + data.distance.toFixed(2) + " m";

        // ⭐ 轨迹
        let points = data.points.map(p => [p.lat, p.lng]);

        route.setLatLngs(points);

        ui.count.innerHTML = "📍轨迹点<br>" + points.length;
        ui.time.innerHTML = "🕒时间<br>" + new Date().toLocaleTimeString();
    });
}

async function resetSystem()
{
    await fetch(`http://${linuxIP}:8080/reset`);

    route.setLatLngs([]);

    ui.distance.innerHTML = "📏距离<br>0 m";
    ui.count.innerHTML = "📍轨迹点<br>0";

    first = true;
    map.setView([0, 0], 2);
}

updateLocation();
setInterval(updateLocation, 3000);