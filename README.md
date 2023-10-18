# Small signal PWM signal generator made with off-the-shelve components
(Made in january 2023)

PWM signal generator based on an ESP32 microcontroller. The user can configure individually each of the 16 PWM signals generated "simultaneously". The configuration is done via a physical & and software user interface made of 4 push buttons and a 1.3" screen displaying a custom GUI. The electronic is encased in a 3D-printed plastic shell.

## Plastic shell
The plastic pieces and the electronic are held together by friction. The pieces can be printed without any supports. 

<img width="580" alt="platic_pieces" src="https://github.com/ldelzott/Tiny-Signal-Generator/assets/78430576/bea9b5c6-d97d-49ed-926f-a97dd501240a">

* A - gen_back_case.step
* B - gen_screen_upper_holder.step
* C - gen_screen_holder.step
* D - gen_pcb_separation.step
* E - gen_keypad_holder.step
* F - gen_keypad_lower_holder.step
* G - gen_front_case.step

## Electronic 
### Components
* [[DOC](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-devkitc.html)] [[Components](https://www.amazon.fr/gp/aw/d/B071P98VTG/?_encoding=UTF8&pd_rd_plhdr=t&aaxitk=8e1f3af7f2845abe382908f56344a9c3&hsa_cr_id=8057106160402&qid=1697656878&sr=1-2-e0fa1fdd-d857-4087-adda-5bd576b25987&ref_=sbx_be_s_sparkle_mcd_asin_1_img&pd_rd_w=RCBxp&content-id=amzn1.sym.fcb06097-6196-4e78-932c-0f6f89d56105%3Aamzn1.sym.fcb06097-6196-4e78-932c-0f6f89d56105&pf_rd_p=fcb06097-6196-4e78-932c-0f6f89d56105&pf_rd_r=A83J5SPJ377MESW0R9X4&pd_rd_wg=WdU1S&pd_rd_r=5c876913-b23a-4dba-9102-64890ab3dd3e&th=1)] ESP32-WROOM-32 Dev Kit C
* [DOC] [[Components](https://www.amazon.fr/daffichage-polychrome-cristaux-liquides-dentraînement/dp/B07QGQ1SR2/ref=sr_1_6?keywords=IPS+1.3"+240x240+with+ST7789&qid=1697656905&sr=8-6)] IPS 1.3" 240x240 with ST7789 controller.
* [DOC] [[Components](https://www.amazon.fr/ARCELI-Interface-IIC-Blindage-Raspberry/dp/B07RG9ZTMD/ref=sr_1_5?__mk_fr_FR=ÅMÅŽÕÑ&crid=3J0AZ9PUXPUG1&keywords=PCA9685+breakout+board&qid=1697656927&sprefix=pca9685+breakout+board%2Caps%2C59&sr=8-5)] PCA9685 breakout board
* [DOC] [[Components](https://www.amazon.fr/ARCELI-Interface-IIC-Blindage-Raspberry/dp/B07RG9ZTMD/ref=sr_1_5?__mk_fr_FR=ÅMÅŽÕÑ&crid=3J0AZ9PUXPUG1&keywords=PCA9685+breakout+board&qid=1697656927&sprefix=pca9685+breakout+board%2Caps%2C59&sr=8-5)] 4 Push buttons


### Schematic (draft)
<img width="666" alt="wire_diagram_draftpng" src="https://github.com/ldelzott/Tiny-Signal-Generator/assets/78430576/cd7d5477-0c07-418e-bb92-7158924d5ae4">

### Specifications
PWM 12-Bit 1KHz, reduced to 50Hz when at least one servo motor is attached & used.
When not disturbed by any user input, the generator output 50 points per seconds for each signal (50x16 points for the 16 signals). Each signal is made of 2048 points.   
## Overview
The device with the front case removed:

<img width="503" alt="overview" src="https://github.com/ldelzott/Tiny-Signal-Generator/assets/78430576/9f161fbf-1abd-46df-8340-5e2623ad2902">




