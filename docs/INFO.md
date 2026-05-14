TFPCX + TSTHOST + BAYCOM MODEM

    Software needed:
    TSTHOST ver.1.43c (TSTH143C.ZIP); (TSTHOST 's Home Page on http://www.r-j.it/servizi/hp/ik1gkj/ik1gkj.htm);
    TFPCX (by DG0FT);

    Use the following Batch file ( 1200 Baud):
    cd\tsthost
    loadhigh tfpcx -pcom1 -b1200
    tsthost /t /i253 /K3 /NOXMS /U200
    tfpcx -pcom1 -u

    1 (directory where is TSTHOST and TFPCX);
    2 (load TFPXC with modem on COM1 and BAUD RATE 1200);
    3 (load TSTHOST using TFPCX o TFPCR driver, interrupt 253 (TFPCX), only 3 channels, no XMS and set unproto list at 200);
    4 (exit unloading TFPCX);

    If you use 300 Baud use the following file:
    cd\tsthost
    loadhigh tfpcx -pcom1 -b300
    tsthost /t /i253 /K3
    tfpcx -u

    (You only need to put -B300 instead of -B1200; I Think it's ok also for 2400 ecc.).

All done, of course, under DOS!

73 de iz7ath, Talino

