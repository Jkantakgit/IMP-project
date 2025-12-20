Dokumentace projektu IMP
Zabezpečovací systém s detekcí pohybu

Zadání projektu
Cílem projektu je návrh a implementace jednoduchého zabezpečovacího systému s detekcí pohybu. Systém je tvořen dvěma zařízeními založenými na platformě ESP32, která spolu komunikují prostřednictvím webového rozhraní. Při detekci pohybu dojde k pořízení fotografie pomocí kamery ESP32-CAM.

Příprava projektu
Studium použitého hardwaru
ESP-EYE / ESP32-CAM
V rámci přípravy bylo nutné seznámit se s možnostmi a omezeními modulu ESP-EYE / ESP32-CAM, zejména s těmito funkcemi:
•	podpora SD karty pro ukládání fotografií,
•	využití PSRAM pro práci s obrazovými daty,
•	připojení k síti pomocí Wi-Fi,
•	ovládání integrovaného LED blesku.
ESP32 a PIR senzor
Dále bylo studováno chování PIR senzoru a jeho propojení s mikrokontrolérem ESP32:
•	PIR senzor automaticky prodlužuje dobu sepnutí při trvajícím pohybu,
•	senzor má tři piny: VCC, VOUT a GND,
•	výstupní pin VOUT poskytuje digitální signál indikující detekci pohybu.

Způsob realizace
Detekční jednotka (PIR senzor + ESP32)
Detekční jednotka je tvořena modulem ESP32 a PIR senzorem. Zapojení senzoru je následující:
•	pin VCC je připojen na napájecí napětí 3,3 V,
•	pin GND je připojen na zem,
•	pin VOUT je připojen na vstupně-výstupní pin IO2 mikrokontroléru ESP32.
Na pinu IO2 je nakonfigurováno přerušení (interrupt), které se vyvolá při detekci pohybu. V okamžiku přerušení ESP32 odešle HTTP požadavek na webový server kamery na endpoint /photo s daty ve formátu:
{ capture: timestamp }
Pokud je požadavek zamítnut z důvodu nesouladu časového okna, zařízení provede synchronizaci času s webovým serverem a následně se pokusí požadavek odeslat znovu.

Kamerová jednotka (ESP32-CAM)
ESP32-CAM slouží jako kamerová jednotka systému a provozuje dva samostatné webové servery:
•	první server zajišťuje zobrazování webové stránky, stahování pořízených snímků a ovládání zařízení,
•	druhý server poskytuje živý obrazový přenos (live feed).
Zařízení si udržuje vlastní čas, který je aktualizován pokaždé, když se jakékoliv zařízení připojí k webovému serveru (při otevření webové stránky). Kamerová jednotka rovněž pracuje s časovým oknem, ve kterém přijímá příkazy k pořízení fotografie. Pokud je požadavek odeslán mimo toto časové okno, není vykonán.
Pořízené fotografie jsou ukládány na SD kartu, zatímco soubory potřebné pro chod webového serveru, jako je hlavní HTML stránka (index) a související prostředky, jsou uloženy v paměti SPIFFS.
Při pořizování fotografie se automaticky aktivuje integrovaný LED blesk, který nejen zlepšuje kvalitu snímku za zhoršených světelných podmínek, ale zároveň slouží jako vizuální indikace toho, že právě probíhá pořízení fotografie.
Funkčnost systému
Navržený zabezpečovací systém slouží k detekci pohybu a následnému pořízení fotografie sledovaného prostoru. Systém je rozdělen do dvou spolupracujících částí – detekční jednotky a kamerové jednotky.
Kamerová jednotka ESP32-CAM po spuštění vytváří vlastní Wi-Fi přístupový bod, který slouží jako hlavní komunikační rozhraní systému. Detekční jednotka tvořená modulem ESP32 s připojeným PIR senzorem se po zapnutí připojí k této bezdrátové síti a následně komunikuje s kamerovou jednotkou prostřednictvím HTTP požadavků. Obě zařízení zároveň inicializují své webové servery.
Kamerová jednotka udržuje aktuální systémový čas, který slouží k validaci příchozích požadavků a k řízení časového okna, ve kterém je možné provádět jednotlivé akce. Čas je zároveň využíván při ukládání fotografií, kdy je součástí názvu souboru a umožňuje tak jednoznačně určit okamžik pořízení snímku.
Jakmile PIR senzor detekuje pohyb, vyvolá přerušení na vstupu mikrokontroléru ESP32. V reakci na tuto událost detekční jednotka odešle HTTP požadavek na kamerovou jednotku s požadavkem na pořízení fotografie.
Kamerová jednotka ověří, zda se požadavek nachází v povoleném časovém okně. Pokud je požadavek platný, dojde k pořízení fotografie pomocí kamery ESP32-CAM. Během pořizování snímku se aktivuje LED blesk, který zlepšuje osvětlení scény a zároveň slouží jako vizuální indikace toho, že fotografie je právě pořizována.
V případě, že je požadavek zamítnut z důvodu nesouladu času, detekční jednotka provede synchronizaci času s kamerovou jednotkou a pokusí se požadavek odeslat znovu.
Systém rovněž umožňuje vzdálený přístup k živému obrazovému přenosu a k uloženým fotografiím prostřednictvím webového rozhraní kamerové jednotky.

