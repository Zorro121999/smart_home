async function updateSensors() {

    try {

        const response = await fetch("/api/sensors");

        if (!response.ok) {
            throw new Error("HTTP Fehler " + response.status);
        }

        const data = await response.json();

        const container = document.getElementById("sensors");

        // Alte Sensor-Karten löschen
        container.innerHTML = "";

        // Alle Sensoren durchlaufen
        data.sensors.forEach(sensor => {

            const card = document.createElement("div");

            card.className = "sensor-card";

            card.innerHTML = `
                <h2>${sensor.name}</h2>

                <div class="label">Temperatur</div>
                <div class="value">
                    ${sensor.temperature.toFixed(2)} °C
                </div>

                <div class="label">Luftfeuchtigkeit</div>
                <div class="value">
                    ${sensor.humidity.toFixed(2)} %
                </div>

                <div class="label">Bodenfeuchtigkeit</div>
                <div class="value">
                    ${sensor.moisture.toFixed(2)} %
                </div>
            `;

            container.appendChild(card);
        });

        document.getElementById("status").textContent =
            "Letzte Aktualisierung: " + new Date().toLocaleTimeString();

    }
    catch (error) {

        console.error(error);

        document.getElementById("status").textContent =
            "Fehler beim Abrufen der Sensordaten";
    }
}


// Sofort beim Laden aktualisieren
updateSensors();


// Danach alle 30 Sekunden
setInterval(updateSensors, 30000);