# 64korppu-E — PCB-errata (2-layer ja 4-layer)

Nämä koskevat molempia levyvariantteja (sama netlist). Kohdat löydetty
katselmoinnissa 2026-07; korjaukset voi tehdä valmiiksi rakennettuun
levyyn ilman uutta valmistuskierrosta.

## 1. R11 (/RDATA pull-up) 10 kΩ → 1 kΩ — SUOSITELTU

Levyllä /RDATA:n pull-up on 10 kΩ (R11). PC-standardi on 1 kΩ.
10 kΩ toimii lyhyellä kaapelilla, mutta nousuaika kasvaa kaapeli-
kapasitanssin myötä (τ = 10 kΩ × ~100 pF ≈ 1 µs), mikä syö marginaalin
HD-signaalin 2 µs pulssivälillä.

**Korjaus**: vaihda R11 → 1 kΩ. (ÄLÄ käytä 150 Ω:ia — se vaatii
asemalta 33 mA nieluvirran, johon kaikki asemat eivät kykene.)

Huom: dokumentaation aiemmin kuvaamaa 74LS14-Schmitt-triggeriä tai
transistoripohjaista "aktiivista pull-upia" EI ole levyllä, eikä niitä
tarvita — ks. docs/E-IEC-Nano-SRAM/piirikaavio.md ja mfm-dekoodaus.md
(KORJAUS-osio). Jos Schmitt-puskuri silti lisätään, muista vaihtaa
firmiksessä ICP1 nousevalle reunalle (mfm_init, ICES1).

## 2. 74HC595 /OE (pin 13) on juotettu suoraan GND:iin — BOOT-RISKI

Dokumentaation boot-suojaus (/OE A3:lle 10 kΩ pull-upilla + RCLK:n
10 kΩ pull-down) EI ole levyllä: /OE on kiinteästi maissa ja RCLK:lla
ei ole pull-downia. Seuraus: Nanon bootin/resetin/flashauksen aikana
shift-rekisterin satunnainen sisältö ohjaa floppyn signaaleja —
/WGATE voi glitchata aktiiviseksi ja **tuhota levyllä olevaa dataa**,
ja /MOTOR//STEP voivat aktivoitua itsekseen.

**Korjaukset (valitse ainakin ensimmäinen):**

1. **RCLK pull-down**: juota 10 kΩ vastus U3:n pinnin 12 (RCLK) ja
   GND:n väliin (esim. suoraan IC-kannan alle). Estää satunnaisen
   latchauksen ennen kuin firmware ajaa D6:n hallitusti.
2. **/OE-mod**: katkaise U3 pin 13:n maadoitus (nosta pinni kannasta
   tai katkaise veto), juota pinnistä johdin Nanon A3:een ja lisää
   10 kΩ pull-up pinnistä +5V:iin. Firmware ohjaa jo A3:a
   (shiftreg_init: /OE LOW vasta kun rekisterissä on 0xFF).

Kunnes korjaus on tehty: **poista levyke asemasta** aina kun flashaat
tai resetoit Nanon.

## 3. U2: 23LC256 vs 23LC512 — TARKISTA MIKÄ PIIRI LEVYLLÄ ON

Skeeman komponentti ja hardware/README sanovat 23LC256 (32 KB),
mutta docs/kokoonpano.md ja firmware olettivat 23LC512 (64 KB).
Firmware tunnistaa nykyään piirin koon käynnistyksessä
(sram_capacity(), UART-lokissa `[SRAM] 32KB/64KB`) ja rajaa
capture-puskurin automaattisesti, joten kumpikin toimii — mutta
23LC512:lla capture kattaa suuremman osan raidasta (vähemmän
uudelleenyrityksiä). Suositus: käytä 23LC512:ta jos saatavilla.

## 4. Floppyn virtaliitin puuttuu levyltä

BOM listaa 4-pin Berg -liittimen floppyn virralle, mutta levyllä
sitä ei ole. Aseman +5V pitää syöttää erikseen samasta 5V/2A
lähteestä (yhteinen GND on IDC-kaapelin kautta, mutta vedä silti
erillinen GND-johdin virtaliittimeen).

## 5. IEC-pinnien järjestys DIN-liittimessä

Ei havaittua vikaa — J1:n pinnijärjestys ja 100 Ω suojavastukset
vastaavat dokumentaatiota. Mainittu tässä, koska DIN-6:n peilikuva
on yleinen virhelähde: jos IEC ei toimi, tarkista liittimen suunta
ensin.
