from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import census_linear_lighting_fxp as census


class ContractError(RuntimeError):
    pass


EXPECTED_BASE_CONTRACTS = {
    0x00000000: ((932, "bda7028e4d023d80b1252229246173e7"), (0x10000000,)),
    0x00000001: ((1060, "bcf6976db0386f17e06b975e451db93d"), (0x10000001,)),
    0x00000004: ((944, "ff037555b0aee4168134f15de151515a"), (0x10000004,)),
    0x00000005: ((1088, "439de92c67b0352ee91fac96a615c4ab"), (0x10000005,)),
    0x00000020: ((924, "fdac9957717fcebecdd718ecb03c131d"), (0x10000020,)),
    0x00000021: ((1052, "26d01ba34d98cb8ee415ef63bcd543bf"), (0x10000021,)),
    0x00000024: ((936, "7f62b1b3a0e9e7e50adcd32b0df8ed06"), (0x10000024,)),
    0x00000025: ((1080, "50201203e8e3ca84c4074068586bee6d"), (0x10000025,)),
    0x00000040: ((1060, "3a83b4a97526664e309cd83a80d118bb"), ()),
    0x00000041: ((1188, "2861e98c14449b07a9417117995159bf"), ()),
    0x00000044: ((1072, "64f79a71a64e454ffcac5f7f6d54a520"), (0x10000044,)),
    0x00000045: ((1216, "6d5a39d1ea65c71b32099a86747bc715"), (0x10000045,)),
    0x00000081: ((1084, "04e462722bb1a86bdfbc4b9826f0ed68"), ()),
    0x00000085: ((1112, "8dca5ca5a7756a38d58e95b4838bfdca"), (0x0000008D,)),
    0x0000008C: ((968, "1b3e9694856b3c18f80855c1d237eabe"), ()),
    0x000000A5: ((1104, "81c854c005564ea37e8c3f82e2f874a4"), (0x000000AD,)),
    0x000000CD: ((1240, "eda2d309bfdfe31b1eec407b74d121f7"), ()),
    0x00001000: ((1524, "e2d4cdae2b18d59df30374a9fcf340b7"), (0x10001000,)),
    0x00001001: ((1656, "1c349753b833673c9bc6a40bd863d599"), (0x00001081,)),
    0x00001004: ((1540, "084e7231aaaa2f214a2124fc6d564877"), ()),
    0x00001005: ((1684, "74563db05c8d5e2ea97008df7eeaa854"), (0x00001085, 0x0000108D)),
    0x00001020: ((1516, "6cf85d3b39f1d4de32745de584ca00fd"), ()),
    0x00001021: ((1648, "864807e5a6f7ed4b0e71a3fd2a70ccd9"), ()),
    0x00001024: ((1532, "204272622eb98e3d554785809f537c59"), ()),
    0x00001025: ((1676, "86fa16f213644c372469126af541fe9b"), (0x000010A5, 0x000010AD, 0x10001025)),
    0x00001041: ((1784, "78fee5142e61cc6f060c960822740410"), ()),
    0x000010CD: ((1812, "a644e812230ba38a4afdb33085c4b84a"), ()),
    0x00100000: ((1544, "c830ab660f4bab472c9db2602c33d66d"), ()),
    0x00100001: ((1672, "6730e79daa7704d05e30149b9725a4ea"), ()),
    0x00100004: ((1556, "91a411a57918ec887bc59721f4d2168e"), ()),
    0x00100005: ((1700, "a6dc248d75bdfe8f0f1764266fad343a"), ()),
    0x00100024: ((1548, "26b4dfe47551fa0aa0a274852722b127"), ()),
    0x00100025: ((1692, "9673a37fe7c6d4c3071b99aa4dd74292"), ()),
    0x01000000: ((1256, "ef30160dd2d3f352351f20fb78c6853f"), (0x02000000,)),
    0x01000024: ((1260, "1d055339b1167a6371139fcbd2306ae9"), (0x02000024,)),
    0x08000004: ((1860, "4abde797d59b8507068b3dbc68cd7c98"), ()),
    0x08100004: ((2420, "57adf3dc6789486ce7fb5e8d6cddca8c"), ()),
    0x40000000: ((960, "5a73a6524a1858ccd6804b02fe907563"), ()),
    0x40000001: ((1088, "931e56ea51a5477fa413958893baa10d"), ()),
    0x40000004: ((972, "4b5d61ebd80ff96a4b7998e9dedd676e"), (0x50000004,)),
    0x40000005: ((1116, "05f36a66e6c8d12f63503c21d9e739cc"), (0x50000005,)),
    0x40000020: ((952, "3f51717c58445d02f5c8cac045482865"), ()),
    0x40000021: ((1080, "22b153e376a940362f83ee9a2b7236e7"), (0x50000021,)),
    0x40000024: ((964, "37624a811e5382a9c6ae58adb78709ec"), (0x50000024,)),
    0x40000025: ((1108, "853a3f852ffce30c801adf1d29c68d22"), (0x50000025,)),
    0x40000045: ((1244, "5dac32845575e042be2c35d540f293a8"), (0x50000045,)),
    0x40000085: ((1140, "172aef6548fb9bc0ecb0e5bafae857fb"), (0x4000008D,)),
    0x400000A5: ((1132, "df00a49cbcddf32dc4a4c3d1df0d11ec"), (0x400000AD,)),
    0x400000CD: ((1268, "69822c1eecb1b1fd3e057848b2d2647c"), ()),
    0x40001000: ((1552, "da3ed211e8851d20333852d48f56512a"), ()),
    0x40001004: ((1568, "ddc101097dae0113063076428ae17d02"), ()),
    0x40001005: ((1712, "be987cfb5b4f95a0b426534ac26cf167"), (0x4000108D,)),
    0x40001024: ((1560, "f9f0de8add272d6e0cdc03aaffedbad0"), ()),
    0x40001025: ((1704, "c70437319cd048bee331c783adfe5323"), (0x400010AD,)),
    0x40100004: ((1584, "7d9d49b8d11e48bdc7d5d94f4da2df2b"), ()),
    0x40100005: ((1728, "1aa660efd00568bf14947444cec280fe"), ()),
    0x48000004: ((1908, "6edde2cd724a7ed5e7d11ad94d18dda2"), ()),
    0x50000044: ((1100, "cb079e03a80dc618d30ceef20abaa0ba"), (0x50000046,)),
    0x00002004: ((1216, "e9462b25c930158800d787decc8772db"), (0x00A02004,)),
    0x00002005: ((1388, "adcd590c482b3ae943abc230429e603d"), ()),
    0x00002024: ((1208, "3d4bd19ef0df8e7a632cb946bbbfba83"), (0x00A02024,)),
    0x00002025: ((1380, "5fb7bddab538381c29fbba209ef12665"), ()),
    0x00002044: ((1344, "dc44b650cf4e698a66e2dcbfaed89d42"), ()),
    0x00002045: ((1516, "9af5fbede25a5ab9b26cec921ae276e6"), ()),
    0x00002085: ((1412, "d1080c7e87f2db5c893f86c0edcb5d2d"), (0x0000208D,)),
    0x000020A5: ((1404, "ff6a9825d7a901aee211cbba19149be1"), (0x000020AD, 0x100020AD)),
    0x00003004: ((1872, "aa1c16f9f6a470db4819aa883bd2e536"), (0x00A03004,)),
    0x00003005: ((1984, "afda2c8ad2963e56a2b2791672f4a928"), (0x0000308D,)),
    0x00003024: ((1864, "20413e57519e4690e734498352e15a31"), ()),
    0x00003025: ((1976, "4575ebeb5bdadc0ba3fbca21fe1d34e4"), (0x000030A5, 0x000030AD, 0x100030AD)),
    0x00004004: ((1172, "edde127d61717c40f9803895ff6fad11"), (0x00804014,)),
    0x00004005: ((1344, "92358fd6791b09fa7518619715dca786"), (0x00804015, 0x10004005)),
    0x00004024: ((1164, "35c2428e781a144c632911abe84c4c2a"), (0x00804034,)),
    0x00004025: ((1336, "3f03b4e5a56c6df4388dcbb647c28d35"), (0x00804035,)),
    0x00004044: ((1300, "1ab27e8c5a2b226a81526d356c4f32e7"), ()),
    0x00004045: ((1472, "18ce62bdfc03123c5e59cdff555b0a3e"), (0x10004045,)),
    0x000040A5: ((1360, "3b601017dd81c90c1449f175b1c517a0"), (0x000040AD,)),
    0x00005004: ((1768, "205e45c5f5398643d56466fc97a27c2a"), ()),
    0x00005005: ((1972, "e40b58d152c2805de1be59c75186c896"), (0x0000508D, 0x00805015, 0x10005005)),
    0x00005021: ((1904, "ed592286d317f0f6377549858e778809"), ()),
    0x00005024: ((1760, "33fb17a7b063e46efe9d91e302990a7f"), (0x10005024,)),
    0x00005025: ((1964, "19a3483a1a4fbde26c7c3beed7784460"), (0x000050A5, 0x000050AD, 0x00805035, 0x10005025)),
    0x00006004: ((1296, "c2c2d1b9d91cb25f73725db20bbc1f81"), ()),
    0x00006005: ((1440, "6021bec6637759f5bca3e11487974daf"), (0x00806015, 0x00A06015, 0x10006005)),
    0x00006024: ((1288, "78a384c5161b6dd3f3554ec699799d4a"), (0x00806034, 0x00A06024, 0x00A06034)),
    0x00006025: ((1432, "f28c734106c3940ad9c660417bc795ec"), (0x00806035, 0x00A06035)),
    0x00006044: ((1424, "89b4065963e8506451b6bb444aa85a66"), ()),
    0x00006045: ((1568, "eee592b7bdafd36835ccdbee385aa28f"), ()),
    0x00006085: ((1464, "17941c150ba2d57446a6bc12b059312c"), (0x0000608D,)),
    0x000060A5: ((1456, "4d05c0f2c9abb33725fcf56c07959d0e"), (0x000060AD,)),
    0x00007004: ((1920, "ef7424411be880d226b1427e2f65e0ad"), (0x00807014, 0x00A07014)),
    0x00007005: ((1984, "24624dc6e32fa2cf2c84c6b30c5fa53e"), (0x00007085, 0x0000708D, 0x00807015, 0x00A07015, 0x00A0711D)),
    0x00007021: ((1976, "eac1da611940b3ffce84c050881fa178"), (0x00007025, 0x000070AD, 0x00807035, 0x00A07035)),
    0x00007024: ((1912, "f7127f6a895a698d3af3da7b671ec46d"), (0x00807034, 0x00A07034)),
    0x40002005: ((1416, "138838fbca213e542b91e084101f5c77"), ()),
    0x40002024: ((1236, "c8996d9df382dcfc24b1e66c9c0d3b0c"), ()),
    0x40002025: ((1408, "602de34154b60204786d1447ee1ae975"), ()),
    0x400020A5: ((1432, "33c5a42c6013fcd5b2a420f9dbc6a3ee"), (0x400020AD,)),
    0x40003005: ((2012, "0cf3826d1ebaf9d3a6739dc3f42896eb"), (0x4000308D,)),
    0x40003024: ((1892, "356b796f3a318ab945658204e123b55d"), ()),
    0x40003025: ((2004, "716dcb373781bf1a79dd30a444cabefa"), (0x400030AD,)),
    0x40004004: ((1200, "7e42ee7d4f31028bd7d9299024fc4085"), ()),
    0x40004005: ((1372, "150bbfd14ba91d5deed65da4afff60ad"), (0x50004005,)),
    0x40004025: ((1364, "b610abe6ad11aa98d878b7aee3c1b914"), (0x40804035,)),
    0x40005004: ((1796, "69e4d2d452434de4df38a14194ce47c3"), ()),
    0x40005024: ((1788, "19569850de9c394f8617f98e40e06ff7"), ()),
    0x40005025: ((1992, "a01d2607f1475f7d9a96b4fd3f2c85d8"), (0x400050AD, 0x40805035)),
    0x40006024: ((1316, "74b847ee00c5b06c5b7a0f91792aaca6"), (0x40A06034,)),
    0x40006025: ((1460, "2750f1bda0b37c70ea9398f34e000135"), (0x40806035,)),
    0x40006045: ((1596, "9f070a08d3b7cfea7a2716513f404b2d"), ()),
    0x40007025: ((2004, "5781610bd42816eeb1ba66ccbaaa5aba"), (0x400070AD, 0x40807035)),
    0x50004045: ((1500, "c1ce998ea4b89361010b986ca9638b63"), ()),
    0x00000400: ((2344, "d9bed98b1e6a839f310fb6af0de47603"), ()),
    0x00000401: ((2472, "626acd5179ed393c445d9795352694bd"), ()),
    0x00000404: ((2388, "9b6aa36df0aaf4ff18258fc7f1b49cd6"), ()),
    0x00000405: ((2532, "d062b9bef64324a8071e7f0be7b48e7e"), (0x10000405,)),
    0x00000420: ((2336, "5b14901c0aead95e54038624e59012ae"), ()),
    0x00000421: ((2464, "44772d65e19de9ebe1084d1681e5e4ae"), ()),
    0x00000424: ((2380, "a5dc84ce93dcc1270b9657b5ddbfed15"), ()),
    0x00000425: ((2524, "3026ce2930fb6b8fd01760aff6ee96d9"), (0x10000425,)),
    0x00000440: ((2472, "f75b838d93d51f8891d0e53953b4bed8"), ()),
    0x00000441: ((2600, "3f03eeefc3d6b270f48d855f76616d0c"), ()),
    0x00000444: ((2516, "1fd9e5e9f5992372951de9f777b60a38"), ()),
    0x00000445: ((2660, "f5377304f6389dea180318e1eed4aaaf"), ()),
    0x00000485: ((2556, "37907f281d01ccb9ed88a8bde9f6843d"), (0x0000048D,)),
    0x000004A5: ((2548, "184a216c4531dfadf5cff5f8f9e086e2"), (0x000004AD,)),
    0x000004CD: ((2684, "4993318ed59895552f473abe3ed46010"), ()),
    0x01000404: ((2712, "87527451c0b29f7050000a7e3602ce7f"), (0x02000404,)),
    0x01000424: ((2704, "fda7536319289e50cecef9806c4bf185"), (0x02000424,)),
    0x40000400: ((2372, "a58ce60c1925b9bee8634f5f75a42826"), ()),
    0x40000404: ((2416, "74683986feadc262d12f17cd099a1683"), ()),
    0x40000405: ((2560, "184836d29605dcc8e2c6c671fb58ed47"), (0x50000405,)),
    0x40000424: ((2408, "490c03316322dab7f6bafc648f3d5b7a"), ()),
    0x40000425: ((2552, "ceccb9e5cdc1984c03e33d14092ffb57"), (0x50000425,)),
    0x4000048D: ((2584, "1ee7be37b117a461a8a4982cca21db06"), ()),
    0x400004A5: ((2576, "7eaf187f1c78da84e04ca9478c5ecf13"), (0x400004AD,)),
    0x41000424: ((2732, "5d978c5ad7bba6d013e1e7ebdee9cd42"), ()),
    0x00001400: ((2968, "61d40633d4308259e9871045db22e3fc"), ()),
    0x00001401: ((3068, "3a91350f3ee63091cb19839d72d9bdf7"), ()),
    0x00001404: ((2984, "6f574b8f986f195cc0209a708fece82e"), ()),
    0x00001405: ((3128, "a03797df0d4d9cfd5043c5c9e5f3a633"), (0x00001485, 0x0000148D)),
    0x00001421: ((3060, "e78d1f001021a17cd6eceefb616b8413"), ()),
    0x00001424: ((2976, "b0b13b04de13bd260ac513ec1e9f28cd"), ()),
    0x00001425: ((3120, "0171828268bdced1976e1c4965ae0bf1"), (0x000014A5, 0x000014AD)),
    0x000014CD: ((3256, "7655bc0150ba152e19599860d6e92e66"), ()),
    0x40001400: ((2996, "03a29c3bffe356ca0bcb423d7a2b0976"), ()),
    0x40001405: ((3156, "9fab97b0ddf182c68572b5d4302bd7a8"), (0x4000148D,)),
    0x40001425: ((3148, "2fb05d110831a524b4c1d4288398ec7c"), (0x400014AD,)),
    0x400014CD: ((3284, "e308e600e00be3e65828a30f98c2d1d1"), ()),
    0x00002404: ((2660, "854d084be9ea75e9b094437881267adf"), (0x10002404,)),
    0x00002405: ((2832, "cbd59505f59fee187cf4b743d734fe30"), (0x10002405,)),
    0x00002420: ((2596, "7ddee09f57a0c473762480fb98dd21c4"), ()),
    0x00002424: ((2652, "e6fb4af291f5184cd1cd59b57ac492a8"), ()),
    0x00002425: ((2824, "fbbdf11a10358edba40e73d85a5bea57"), ()),
    0x0000248D: ((2856, "a65c2461e6b46c4e8b3e5d3b2e8a7f75"), ()),
    0x000024AD: ((2848, "f2ba91b0907accc39a85cccd922202f3"), ()),
    0x40002404: ((2688, "b6ab271830319adad9cdbab71bdc5a4d"), (0x50002404,)),
    0x40002405: ((2860, "5da7e86d6546a03da97b71708dba0bc0"), (0x50002405,)),
    0x40002424: ((2680, "4f1bd815244f9e2856081cb4af033c20"), ()),
    0x40002425: ((2852, "0e89c9a1e8d5092a9416d1b72273572c"), ()),
    0x4000248D: ((2884, "721166043793e7f619f7ca395be36791"), ()),
    0x400024AD: ((2876, "0b01c307a75cca2c1ee02e0022cdcd3c"), ()),
    0x00003404: ((3316, "e0de1615b31f12042f3fc3ebcf66126e"), ()),
    0x00003405: ((3428, "b2acd34b4edaa92778acb525111143af"), (0x00003485, 0x0000348D)),
    0x00003424: ((3308, "0c22e295cbf619ded0fb1cc5bdc8bfde"), ()),
    0x00003425: ((3420, "aa5dfb59c0833d6cd679b7f1357d0bca"), (0x000034A5, 0x000034AD)),
    0x40003405: ((3456, "c419b33d0126cc2410eab57eaa69cf7a"), (0x40003485, 0x4000348D)),
    0x400034A5: ((3448, "9873b192d6993fc22d5ba0de290fa3af"), (0x400034AD,)),
    0x00004404: ((2616, "f95bdcd94469c1a6128c2d299ba58c92"), ()),
    0x00004405: ((2788, "0263a33cf9da75e4b4e1b2b370756a3d"), ()),
    0x00004425: ((2780, "39ad150d0790b90fabeed34b3caacf18"), (0x00804435,)),
    0x0000448D: ((2812, "f7af70090564518e1307c7993aa30e59"), ()),
    0x000044AD: ((2804, "095be87f69430284059d51d489863298"), ()),
    0x40004405: ((2816, "ee7d035fdded0875542b25bd88abb898"), ()),
    0x4000448D: ((2840, "2ef70b8117fd5de27a796424d1aee970"), ()),
    0x400044AD: ((2832, "c9de37b64fda56cba11bc94299b9a915"), ()),
    0x00005404: ((3212, "fd5388cceb96732644bceb217b835aee"), ()),
    0x00005405: ((3384, "e779c646dbcba4d0a2e059403decf330"), (0x00005485, 0x0000548D, 0x00805415)),
    0x00005425: ((3376, "a7f8f873653258c4e108597d87762efe"), (0x000054AD, 0x00805435)),
    0x40005404: ((3240, "839767c3165490c2cdba0a580042857f"), ()),
    0x40005405: ((3412, "49cc70c29f20c7d9cc4217e05fdcc7ce"), (0x4000548D,)),
    0x40005425: ((3404, "f7de292a5b8869ec80fd6166c34442f3"), (0x400054AD, 0x40805435)),
    0x00006404: ((2740, "6048debbd4075e40fa65760d3bf12e80"), ()),
    0x00006405: ((2884, "0b766814fe08f5f97899dd176f77143d"), (0x00806415,)),
    0x00006424: ((2732, "ff4d09dd9727af61d7328c168a2fb77e"), (0x00A06424, 0x00A06434)),
    0x00006425: ((2876, "9f406a81db9ceee708c7395b420a1008"), (0x00806435,)),
    0x0000648D: ((2908, "60d8736d0e0bd841f6a5bd5582d2ca8a"), ()),
    0x000064AD: ((2900, "2e88fc3699cff7f5f1eb4905287faa80"), ()),
    0x40006404: ((2768, "90b972a49dcc6ec12030178cadbeb09f"), ()),
    0x40006405: ((2912, "9a04418488f956beb9896cea86c07e2f"), (0x40806415,)),
    0x4000648D: ((2936, "b6fdf6807763822aaeab837e620d0061"), ()),
    0x400064AD: ((2928, "9a8e46f2e3feddce7f7a4956143183d3"), ()),
    0x00007401: ((3508, "fc0f10d1cc675f1b41c91ef0ae8c3149"), (0x00007405, 0x0000748D, 0x00807415, 0x00A07405, 0x00A07415)),
    0x00007404: ((3364, "0a1ea008beecdac85d9284bedd29d47e"), (0x00807414,)),
    0x00007424: ((3356, "3c16b7f023421e7060669e0de216d57f"), (0x00807434,)),
    0x00007425: ((3500, "e7a0072176fcc0d9c5ac34042f300836"), (0x000074AD, 0x00807435, 0x00A07435)),
    0x40007405: ((3536, "1db2bac74ba53ca13efea0dffb5a44a2"), (0x4000748D, 0x40807415)),
    0x400074AD: ((3528, "77a1fb32bdf188a8849cbd09612fc468"), (0x40807435,)),
    0x0000408D: ((1368, "6b4c82d621c626f750d3dcd71ceb83d2"), ()),
    0x4000208D: ((1440, "774f07c904adb13dd558eaffadec2819"), ()),
    0x400040AD: ((1388, "622ee4a30a2e0a24b4791f7db3a2d287"), ()),
    0x4000508D: ((2000, "606e9d39ad249d233c85adbbd06ee492"), (0x40805015,)),
    0x4000608D: ((1492, "21abf267a19f59abb8a59ba398d0e92c"), ()),
    0x400060AD: ((1484, "dd12e8ebee3f562aad087233879f389d"), ()),
    0x4000708D: ((2012, "5e0de32f3b0fa8697140b0ece2bf7710"), (0x40807015,)),
    0x00800010: ((984, "7b749050b4fffa95a44368ba214d2992"), ()),
    0x00800011: ((1128, "44cc9ccebd57f1508760318c1833d097"), ()),
    0x00800014: ((992, "47f71bbfd1bc4477f67d612f2bf28843"), (0x10800014,)),
    0x00800015: ((1136, "7ba5dc23b870149b1dc111f4251227b3"), ()),
    0x00800030: ((976, "9a546c3abfda471145e9c380937d660b"), ()),
    0x00800031: ((1120, "8d88d6cea162492bf1db8a830f3106ce"), ()),
    0x00800034: ((984, "cacf497bbd11d0a9404de97fd8473df0"), (0x10800034,)),
    0x00800035: ((1128, "da384e758cb7ba215d144eeb5a87f9d4"), ()),
    0x00800410: ((2396, "ca630f11487e47fdb7a52f18e42bc104"), ()),
    0x00800411: ((2540, "4ccb38b5ba07b2dbd7416a1e2b8697d8"), ()),
    0x00800414: ((2436, "9d568eea9de24547db94064e622f87b7"), ()),
    0x00800415: ((2580, "9de28278bb5644df0c137e835d6cb3ca"), ()),
    0x00800430: ((2388, "25c3dbd9431bc06bc8c12449d941a7ed"), ()),
    0x00800431: ((2532, "af0b8cc68ca12093d79d3e20f28f48e5"), ()),
    0x00800434: ((2428, "770e1dfd794ac6f4ecf15419dd7c233c"), ()),
    0x00800435: ((2572, "0f4a43b0c7d012d2b3a3edb2e8c67d5c"), ()),
    0x00801010: ((1580, "08f61b62a80de7cc445b639d29d1fe72"), (0x10801010,)),
    0x00801011: ((1724, "59cd067ce5fe96587c10a7075a7d0b3a"), ()),
    0x00801014: ((1588, "fe1e503120997dbaeb8b1ad6fa444023"), ()),
    0x00801015: ((1732, "6640a879559348b04f4cf047b9ddc2a6"), ()),
    0x00801030: ((1572, "a410f3fd993c0751550e9828def6815d"), (0x10801030,)),
    0x00801031: ((1716, "1dcd59a15e30d022bb161dbe9d43d7d6"), ()),
    0x00801034: ((1580, "06f2ea497927aa4b5131a0de3768f660"), ()),
    0x00801035: ((1724, "9c219e066d5aac2a211ff8b881b1df71"), (0x10801035,)),
    0x00801055: ((1860, "3ac165c287b82d4f7759b53c8791db2c"), ()),
    0x00801411: ((3136, "653d898ffe8784ca8a786332cb1226ea"), ()),
    0x00801414: ((3032, "e01ab72a7338a82230dbe12595b7912f"), ()),
    0x00801415: ((3176, "850a5061d57b6b2c9d0c6cccfa633375"), ()),
    0x00801430: ((2984, "d6ea9484a77fa1cda288a272a3725d99"), ()),
    0x00801431: ((3128, "e93759dafcba38bb3611d9efecddd830"), ()),
    0x00801434: ((3024, "f3f4b2d990a100af5216998818227261"), ()),
    0x00801435: ((3168, "24c643322a02eac034b3a5203186a818"), ()),
    0x00802014: ((1244, "edc2342c5c29dbd97921bb7dde02a352"), (0x00A02014,)),
    0x00802015: ((1416, "26b03971ea05cb4c417549e614fb6c01"), (0x00A02015,)),
    0x00802034: ((1236, "3ec81029f5e758590fc396db08d34be9"), ()),
    0x00802035: ((1408, "4415dd0d74c4ea4bb8b68b7e93bdc419"), (0x00A02035,)),
    0x00802415: ((2860, "27e05f95ff1047ea647859e55c024a57"), ()),
    0x00802435: ((2852, "48a38544d48fbb107e231d39d68f4d70"), (0x00A02435,)),
    0x00803015: ((2012, "df51d6f31c78a0c79941183483130c95"), (0x00A03015,)),
    0x00803034: ((1892, "ddf88b68a5f3c8541d0b9de7212868ff"), ()),
    0x00803035: ((2004, "273b58f776f67967e35048bec87d8083"), (0x00A03035,)),
    0x00803414: ((3344, "a5ec1e2d3cfb4ba56d6ac3c58154827a"), ()),
    0x00803415: ((3456, "22d7625147b92c58ec565285cc039b9a"), (0x00A03415,)),
    0x00803434: ((3336, "5f296807ea77ef7235a3b6dd86e7b581"), ()),
    0x00803435: ((3448, "b0a03b0467affe94b2aba1e298bb3940"), ()),
    0x00804434: ((2608, "f2568d26901ff6a13a9892b11c6be99c"), ()),
    0x00805455: ((3512, "5291bc41d334d7e2c64ae2dfd2cb22f6"), ()),
    0x00A00004: ((972, "7f43688268389b77593d339921152ea8"), ()),
    0x00A00005: ((1116, "97d501397e40e9ac0736fe51861131d0"), ()),
    0x00A00011: ((1088, "ebe572e9871b1517d310211ef5fdacef"), ()),
    0x00A00014: ((972, "0550612286c68eb2f78c5f4d80838dce"), ()),
    0x00A00020: ((976, "c3f23de5bcf887613d907fbddcc07c33"), ()),
    0x00A00024: ((964, "95e9062e28063c24983d4c74783bf5ab"), ()),
    0x00A00030: ((936, "38f4017aefccd703a902b014218ac0a1"), ()),
    0x00A00031: ((1080, "557e8da159754a1a103d63e0bc30c2e9"), ()),
    0x00A00035: ((1108, "4855258dea69f98e76d9a2d36d6f74c5"), (0x10A00035,)),
    0x00A00404: ((2416, "bf48cb1426109ac619eec635af28c869"), ()),
    0x00A00405: ((2560, "415a8e573c147269d57333948ac1f795"), ()),
    0x00A00414: ((2416, "221096afce9a1b563f89f01d2456b94e"), ()),
    0x00A00424: ((2408, "7eea6ea2c337802ac20db0cf68a60285"), ()),
    0x00A00430: ((2348, "875b7452402d3523e40f1c1f410b772a"), ()),
    0x00A01005: ((1712, "1c91797df324710a66ee94061e29c2e0"), ()),
    0x00A01014: ((1568, "890c5d18df11dbe7ef104274fa9481bc"), ()),
    0x00A01015: ((1712, "09de654de8ffe6228678795e18033eb8"), ()),
    0x00A01024: ((1560, "9c44525e2ca83da2032c1e366a97e1a2"), ()),
    0x00A01025: ((1704, "cb5b983d5068fd67f7fce6a030854c71"), ()),
    0x00A01030: ((1532, "8c1757c7fe78604e41ac03473421f0aa"), ()),
    0x00A01034: ((1560, "d9a22b232ac4dab90d7f771dc35c76b4"), ()),
    0x00A01035: ((1704, "0122f6215b0d82930b8359c2463c649e"), ()),
    0x00A01401: ((3136, "972b8eafa0f82c2c75768a3b0f5812ef"), ()),
    0x00A01424: ((3004, "4ccf84664f68736e5dc26a2e671f995e"), ()),
    0x00A01431: ((3088, "5f6d18788ca570b2bfcb3ea4416a0213"), ()),
    0x00A04015: ((1372, "c9762c339cd29c6d43315c596edc322b"), ()),
    0x00A04034: ((1192, "ec0eda4a83e92a54d99db11890e04b97"), ()),
    0x00A04035: ((1364, "2ebb7e7e964426be4e09fe56f92bc6a2"), ()),
    0x00A05005: ((2000, "4529a3d77a238d50a0ca6ee983704b0b"), (0x00A05015,)),
    0x00A05014: ((1796, "c48bb4bf6928fc9aa776e5c45d6f6358"), ()),
    0x00A05035: ((1992, "3afa29a2739021d94e69698c97018b7a"), ()),
    0x00A05055: ((2128, "8fca1a77a08a4f4536b5c41f68bfe649"), ()),
    0x00A05414: ((3240, "b62e41e441296f4dc7d05b4cbcfb8276"), ()),
    0x00A05415: ((3412, "b3d339a9f9dcc14b95f4820b03f5a500"), ()),
    0x00A05435: ((3404, "f49556dcc8004576a6ab100643c04ee3"), ()),
    0x00A07055: ((2112, "a99c3b726a4b28b113a1a7e28280c2a8"), ()),
    0x01001414: ((3356, "5e8a4a7c73ab8efc8961a2e7d50e84ec"), (0x02001414,)),
    0x40800010: ((1012, "b46824435f34ad9b84651a01a2ce82f4"), ()),
    0x40800011: ((1156, "91aa43689c65e69d2a90c32fa78fdf69"), ()),
    0x40800014: ((1020, "07eeaf50974b0ba8f5698c1b54925dd7"), ()),
    0x40800015: ((1164, "991e15dff88598253b5b96952e5c2150"), ()),
    0x40800030: ((1004, "5c04622b282d3756d4e85dbc9d37bed8"), ()),
    0x40800031: ((1148, "e94da29c01de594e18a1c4d3f20305b7"), ()),
    0x40800034: ((1012, "a06d7d7e4269dabcf42acca764d90df1"), ()),
    0x40800035: ((1156, "068b3ea46b97183635bc27a7b2c665cc"), ()),
    0x40800414: ((2464, "d5766d4dbda29fb0b289d31cb3e0ff21"), ()),
    0x40800415: ((2608, "a8a7ae57b9980663c95192b249cdcd97"), ()),
    0x40800430: ((2416, "a8d82828e60196436f32f9d8b3aa9a84"), ()),
    0x40800434: ((2456, "944d554923fb279e75d1513b29f52efe"), ()),
    0x40800435: ((2600, "f18decdac20d34dd401320c4d9ac4cfb"), ()),
    0x40801015: ((1760, "714b5fe5d32134dbae29120d8b826297"), ()),
    0x40801031: ((1744, "0f95b3959af128419b4e793d76de3453"), ()),
    0x40801034: ((1608, "bf6769538603b1882b0a812b3448864d"), ()),
    0x40801035: ((1752, "6ce325f401394fc0d9f4d64915440cc4"), (0x50801035,)),
    0x40801415: ((3204, "f4efd5fb6c8786781b89eb61de8632e1"), ()),
    0x40801431: ((3156, "654c41eacdaaf7ab532259028f395dbf"), ()),
    0x40801435: ((3196, "2c55b6b51e89be6c74f784affa08c8e6"), ()),
    0x40802034: ((1264, "025d345e7e5098e825e1bab2be2ae6ef"), ()),
    0x40802035: ((1436, "65bab06a41f37470d38be521cd28fdcc"), ()),
    0x40802415: ((2888, "5fcc484bbe6f76bbd4a12aa33f5a5718"), ()),
    0x40802435: ((2880, "490aafd6193a5e9729a763f859613d2d"), ()),
    0x40803015: ((2040, "6ab3b6f5a646719006d261f3fd6fa386"), ()),
    0x40803034: ((1920, "310f50047843c1bd17110b9bcdbca3a4"), ()),
    0x40803035: ((2032, "59d24938731a8d57e05caff6a9b75e4a"), ()),
    0x40803414: ((3372, "a2d45ef14044ddf122bd15ea4f48d7b0"), ()),
    0x40803415: ((3484, "3dd593a66914efe31532e06983701397"), ()),
    0x40803435: ((3476, "bd6a1ae1fcf56b4f6d00a3f67a4c919d"), ()),
    0x40804435: ((2808, "6e3ff4dd601b1eb70674f7baa55c00e5"), ()),
    0x40806015: ((1468, "734c4423abe1f460a36a547d87fd59d0"), ()),
    0x40806435: ((2904, "ee9a04f7b2977a607ff3887e1f264480"), ()),
    0x40807034: ((1940, "f31fa7ae662846e2929c8c483933f493"), ()),
    0x40A01014: ((1596, "ba29627ae101808e15905db8fe36b06a"), ()),
    0x40A02014: ((1272, "f08650d1c2ccf488027c3910610d8519"), ()),
    0x40A04035: ((1392, "c42ed8a02989d63673f4b0b6f5282b1d"), ()),
    0x40A05014: ((1824, "480121cffd99d32fb359fb860ee7ecd8"), ()),
    0x40A05015: ((2028, "acfd2f3fad2cc084aa24994c1ad33354"), ()),
    0x40A05035: ((2020, "13c92ec62cddfb2a0e47a9c8c5f36fd6"), ()),
    0x40A05415: ((3440, "8eb725a2bf8c3d8dc119528203ecf329"), ()),
    0x40A06424: ((2760, "f0ad5a3f5673241d71e91ce3912937d3"), ()),
    0x40A07014: ((1948, "d8e829e7752d788edf2098b038e0e3e0"), ()),
    0x00000206: ((1304, "58a970909dc0b29d32ae60efc17ec52c"), ()),
    0x00000226: ((1296, "1e8d2143e5492e6046b828c84b174045"), ()),
    0x00002206: ((1544, "5ae7b12030941fe6f2373bed834e1993"), ()),
    0x00002226: ((1536, "33e57729fce0014e4c7e1b6ab75a2ead"), ()),
    0x00006206: ((1620, "c9817412fd0360d052558138fe08eb07"), ()),
    0x00006226: ((1612, "7ccfc5d94c122e6b4ac74069304d46ed"), (0x00006A26,)),
    0x00006227: ((1756, "5f592b208dd1c14e1f7b2e14de4b5adc"), ()),
    0x00020204: ((1260, "1dfa6493b7bcab846abc115dfbc21c64"), ()),
    0x00020226: ((1444, "178bb62d7afde6fecb2de01072feb48d"), ()),
    0x00026226: ((1760, "8cbc7adc6e3722d3f3b9c70ff07f1ff7"), ()),
    0x00800204: ((1016, "43d77b0f072a9d148472645c652632ee"), (0x00800206, 0x00800A04, 0x00800A06)),
    0x00800205: ((1156, "6a6e7f479d8f710b83fd22b0349c4704"), (0x00800207, 0x00800A07)),
    0x00800224: ((1008, "d0215ab491156bcf4d7080d17e605341"), (0x00800226, 0x00800A24, 0x00800A26)),
    0x00800225: ((1148, "d06e6422b3d756a69b0279abdae98fe0"), (0x00800227, 0x00800A25, 0x00800A27)),
    0x00800604: ((1040, "1b8c8791237e62362a2389ab836ae2b1"), (0x00800606,)),
    0x00800624: ((1032, "898294f6b6432d394a8913b942840b26"), (0x00800626,)),
    0x00800625: ((1172, "d38917356529d13c32f647ce6ae4688b"), (0x00800627,)),
    0x00802204: ((1256, "7b0f3715967213c38bd7142403e0c6e7"), (0x00802206,)),
    0x00802205: ((1428, "b45250b9c975a137420f223852342954"), (0x00802207,)),
    0x00802224: ((1248, "fc9f2f24b1ee09dcae6519e3e6258bb2"), (0x00802226, 0x00802A24, 0x00802A26)),
    0x00802225: ((1420, "ea48249cb161c79ecd854dc014804fda"), (0x00802227, 0x00802A27)),
    0x00802604: ((1280, "93d882029c830d8f9b8690d935e4e9a0"), (0x00802606,)),
    0x00802605: ((1452, "71f57fd9a706d0bd658b22a18cc8b052"), (0x00802607,)),
    0x00802624: ((1272, "aa5d362fc0e02f5ec8a35cee5c2318d2"), (0x00802626,)),
    0x00802625: ((1444, "c60579171f1bced4550bcb32d2ed30a3"), (0x00802627,)),
    0x00804204: ((1208, "fa33cdf4fba5857fd600da24cfb01a1b"), (0x00804206,)),
    0x00804224: ((1200, "54cc18c3b0917335e5a50e93f6ec0d5a"), (0x00804226, 0x00804A24, 0x00804A26)),
    0x00804225: ((1372, "0c8924761e5b0a84571dae5941c029fe"), (0x00804227, 0x00804A27)),
    0x00804604: ((1232, "9f5ba2b29db7c2f459578024f0ab168f"), (0x00804606,)),
    0x00804605: ((1404, "78414d4d7bada9fbeaa0d543a866c3a1"), (0x00804607,)),
    0x00804624: ((1224, "78837b7025be8b8a947c19ee88018a83"), (0x00804626,)),
    0x00806204: ((1332, "bf4730572fd92d55543af1b802e4fd26"), (0x00806206, 0x00806A04, 0x00806A06, 0x0080E204, 0x0080E206)),
    0x00806205: ((1476, "0cc2cb6de4b332412b2970c07b968185"), (0x00806207, 0x00806A05, 0x00806A07)),
    0x00806224: ((1324, "d0fa2a45493d4b25bed3336dbb0c5e30"), (0x00806226, 0x00806A24, 0x00806A26, 0x0080EA24, 0x0080EA26)),
    0x00806225: ((1468, "b8be15b256531d678697f76ef6873001"), (0x00806227, 0x00806A25, 0x00806A27)),
    0x00806604: ((1356, "931eb85f13a3e407d56c82ee44fda5e6"), (0x00806606, 0x0080EE06)),
    0x00820204: ((1164, "42b4fe434d3c0adfab3c876a8632dd39"), (0x00820206,)),
    0x00820205: ((1312, "5c48af5f028cfecf96eefa8901020989"), (0x00820207, 0x00820A07)),
    0x00820224: ((1156, "df31cfcea69ead0b4bd44108be93b8b6"), (0x00820226, 0x00820A24, 0x00820A26)),
    0x00820225: ((1304, "344671a09612cddfa316ef04a03d80e8"), (0x00820227, 0x00820A25, 0x00820A27)),
    0x00820606: ((1188, "bf30039a0203f5047f7548e5f80a675b"), ()),
    0x00820607: ((1336, "b478fe5b83ca3d28e9b09d0855e8c7b3"), ()),
    0x00820624: ((1180, "5c4a8e9f8752a720362c98ad4d69263c"), (0x00820626,)),
    0x00820625: ((1328, "b588fa20fb10ab73a60a2fbf309ccb53"), (0x00820627,)),
    0x00822204: ((1404, "a29dd32ebdeb9ad91c419df7ed4a92e1"), (0x00822206,)),
    0x00822205: ((1584, "5667edea7877b7bcf25f8085db5c9536"), (0x00822207,)),
    0x00822224: ((1396, "b9c2d7ef9b1b1ac175fc720fb0daa0fe"), (0x00822226, 0x00822A24, 0x00822A26)),
    0x00822225: ((1576, "2953f9740d5c529a92c8c01c8214f2d6"), (0x00822227, 0x00822A27)),
    0x00822604: ((1428, "6428926d34bf0421163bf93c671792aa"), (0x00822606,)),
    0x00822605: ((1608, "989079bc71a41330ae9fde8fee08446e"), (0x00822607,)),
    0x00822625: ((1600, "76b8d9723c7f803cc40d5e51db38a8c0"), (0x00822627,)),
    0x00822626: ((1420, "131d0f554f3234418c3c125a99305e1a"), ()),
    0x00824206: ((1356, "341081f7f49193e0cb43b5f14eab7e93"), ()),
    0x00824207: ((1584, "0fcca697b10bae3be4a73a8aa147b3bc"), ()),
    0x00824224: ((1348, "a6d1cfcba9a1acd201504c7e4348fe3c"), (0x00824226, 0x00824A24, 0x00824A26)),
    0x00824225: ((1576, "85d603af82886102b2ecf5207fecc174"), (0x00824227, 0x00824A27)),
    0x00824604: ((1380, "7c11ad809d13810497cf21c5822b4bda"), (0x00824606,)),
    0x00824605: ((1608, "87fc1fc5c4ac4b02ec5dab08487b0e5a"), (0x00824607,)),
    0x00824627: ((1600, "0eacec4cd53359983c151de5205827b2"), ()),
    0x00826204: ((1480, "effcc700f76aa498708ed48e0759b8b5"), (0x00826206, 0x00826A04, 0x00826A06, 0x0082E204, 0x0082E206)),
    0x00826205: ((1632, "d60c55721e17dad5287f6bf15048dd16"), (0x00826207, 0x00826A05, 0x00826A07, 0x0082E207)),
    0x00826224: ((1472, "1dfe62c0e8bd10217ee2493eff9489d6"), (0x00826226, 0x00826A24, 0x00826A26)),
    0x00826225: ((1624, "86cfc9170b144456a6fc44efcfaf089e"), (0x00826227, 0x00826A25, 0x00826A27, 0x0082EA27)),
    0x00826606: ((1504, "b1f664578b32051829b20f0369e4c094"), (0x0082EE06,)),
    0x00826607: ((1656, "a69f59e58c9890dafda000a2ebd49407"), (0x0082EE07,)),
    0x00800607: ((1180, "d913ab0769ff5d96a44c70a398a7e150"), ()),
    0x00804207: ((1380, "cab127143669d5acf8ca66ec11d77d4a"), ()),
    0x00804627: ((1396, "b196e6a6ca2786e1865a93db3d94f805"), ()),
    0x00806607: ((1500, "86414601a85a943ab3be8fc0fb81a7b4"), ()),
    0x00808204: ((1036, "7a6ab7c7ddd6c895a428cda955d79ba2"), (0x00808206,)),
    0x00808205: ((1176, "82fc54554a079870da0873c2359ce1f3"), (0x00808207,)),
    0x00808226: ((1028, "3cc735e793965f19222431de566bf6ca"), ()),
    0x00808227: ((1168, "006877fb3f61b6ca2d5e8f526b22e85c"), ()),
    0x0080A226: ((1252, "052973910cb9255bc6666f009d4c9625"), ()),
    0x0080A227: ((1392, "c14c09c331982f405319baa2d0deb4d2"), ()),
    0x0080E205: ((1396, "599a4b1c82b961974cc7837899e6d0fd"), (0x0080E207,)),
    0x0080EA27: ((1388, "1638bc081c27788bf658c85114011510"), ()),
    0x0080EE07: ((1420, "fbd4a17409440e41ad1d9a5ab2f1a6e3"), ()),
    0x00828204: ((1184, "15ae718e2ba1ae0ba9ede030d92ce62b"), (0x00828206,)),
    0x00828205: ((1332, "6b788eae41bb6dd44742f85428b3bc84"), (0x00828207,)),
    0x00828226: ((1176, "76f8492ddde12eb2408237c1bcb53ddc"), ()),
    0x00828227: ((1324, "1c51d086c2b1bccc24657cf62c631520"), ()),
    0x0082A226: ((1400, "31c164f95dfe95a12a21c3296cae05f8"), ()),
    0x0082A227: ((1548, "94c9b1a0e38f5f1d3c0546af057f2956"), ()),
}

EXPECTED_EFFECT_CONTRACT_COUNT = 631
EXPECTED_ENVMAP_SLOT_COUNT = 183
EXPECTED_ENVMAP_IDENTITY_COUNT = 155
EXPECTED_ENVMAP_FXP_DIGEST = (
    "2c501c802ed4b0ccf6c3cfb75f4ea8c6a3ab929cdc0ed166dbffa10d02fe6861"
)
EXPECTED_PARTICLE_DISTORTION_SLOT_COUNT = 63
EXPECTED_PARTICLE_DISTORTION_IDENTITY_COUNT = 60
EXPECTED_PARTICLE_DISTORTION_FXP_DIGEST = (
    "e39a418bad09881edcb3313a5d6802ee9c6a4bc4ad16b6452a1e2e548d73c91f"
)


def frozen_effect_family(
    inventory: census.FxpInventory,
    flag: int,
    expected_slot_count: int,
    expected_identity_count: int,
    expected_digest: str,
    label: str,
) -> list[census.DxbcContainer]:
    records = sorted(
        (
            item
            for item in inventory.containers
            if item.family == "Effect"
            and item.stage == "PS"
            and item.key is not None
            and item.key & flag
        ),
        key=lambda item: int(item.key),
    )
    identities = {item.identity for item in records}
    if (
        len(records) != expected_slot_count
        or len(identities) != expected_identity_count
    ):
        raise ContractError(
            f"active FO4VR FXP Effect {label} family changed shape"
        )

    digest = hashlib.sha256()
    for item in records:
        digest.update(struct.pack("<II", int(item.key), len(item.data)))
        digest.update(item.data)
    if digest.hexdigest() != expected_digest:
        raise ContractError(
            f"active FO4VR FXP Effect {label} family changed bytecode"
        )
    return records


def expected_contracts(
    inventory: census.FxpInventory,
) -> dict[int, tuple[tuple[int, str], tuple[int, ...]]]:
    envmap_records = frozen_effect_family(
        inventory,
        0x00080000,
        EXPECTED_ENVMAP_SLOT_COUNT,
        EXPECTED_ENVMAP_IDENTITY_COUNT,
        EXPECTED_ENVMAP_FXP_DIGEST,
        "envmap",
    )
    particle_distortion_records = frozen_effect_family(
        inventory,
        0x00400000,
        EXPECTED_PARTICLE_DISTORTION_SLOT_COUNT,
        EXPECTED_PARTICLE_DISTORTION_IDENTITY_COUNT,
        EXPECTED_PARTICLE_DISTORTION_FXP_DIGEST,
        "particle-distortion",
    )

    result = dict(EXPECTED_BASE_CONTRACTS)
    for label, records in (
        ("envmap", envmap_records),
        ("particle-distortion", particle_distortion_records),
    ):
        keys_by_identity: dict[tuple[int, str], list[int]] = {}
        for item in records:
            keys_by_identity.setdefault(item.identity, []).append(int(item.key))
        for identity, keys in keys_by_identity.items():
            ordered_keys = sorted(keys)
            descriptor = ordered_keys[0]
            if descriptor in result:
                raise ContractError(
                    f"Effect {label} descriptor overlaps existing contract: "
                    f"0x{descriptor:08X}"
                )
            result[descriptor] = (identity, tuple(ordered_keys[1:]))
    if len(result) != EXPECTED_EFFECT_CONTRACT_COUNT:
        raise ContractError("Effect contract matrix changed size")
    return result


def read_manifest(
    root: Path,
    contracts: dict[int, tuple[tuple[int, str], tuple[int, ...]]],
) -> list[dict[str, object]]:
    path = (
        root
        / "package"
        / "Shaders"
        / "Community"
        / "EffectLinearLightingContracts.json"
    )
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, list) or len(value) != len(contracts):
        raise ContractError(
            f"Effect manifest must contain {len(contracts)} contracts"
        )

    names: set[str] = set()
    descriptors: set[int] = set()
    resources: set[str] = set()
    for index, entry in enumerate(value):
        if not isinstance(entry, dict):
            raise ContractError(f"Effect manifest entry {index} is not an object")
        name = entry.get("name")
        descriptor = entry.get("descriptor")
        aliases = entry.get("aliases")
        resource = entry.get("resource")
        if not isinstance(name, str) or not name:
            raise ContractError(f"Effect manifest entry {index} has an invalid name")
        if not isinstance(descriptor, int) or descriptor not in contracts:
            raise ContractError(
                f"Effect manifest entry {index} has an invalid descriptor"
            )
        expected_aliases = list(contracts[descriptor][1])
        if aliases != expected_aliases:
            raise ContractError(
                f"Effect manifest entry {index} has unexpected descriptor aliases"
            )
        if not isinstance(resource, str) or not resource.startswith("IDR_"):
            raise ContractError(
                f"Effect manifest entry {index} has an invalid resource"
            )
        if name in names or descriptor in descriptors or resource in resources:
            raise ContractError(f"Effect manifest entry {index} is duplicated")
        names.add(name)
        descriptors.add(descriptor)
        resources.add(resource)
    if descriptors != set(contracts):
        raise ContractError("Effect manifest descriptor matrix is incomplete")
    return sorted(value, key=lambda item: int(item["descriptor"]))


def effect_originals(
    inventory: census.FxpInventory,
    contracts: dict[int, tuple[tuple[int, str], tuple[int, ...]]],
) -> dict[int, census.DxbcContainer]:
    expected_keys = {
        key
        for descriptor, (_, aliases) in contracts.items()
        for key in (descriptor, *aliases)
    }
    records = [
        item
        for item in inventory.containers
        if item.family == "Effect"
        and item.stage == "PS"
        and item.key in expected_keys
    ]
    if len(records) != len(expected_keys) or {
        int(item.key) for item in records
    } != expected_keys:
        raise ContractError(
            "active FO4VR FXP basic Effect descriptor matrix changed"
        )
    by_key = {int(item.key): item for item in records}
    originals: dict[int, census.DxbcContainer] = {}
    for descriptor, (identity, aliases) in contracts.items():
        keys = (descriptor, *aliases)
        if any(by_key[key].identity != identity for key in keys):
            raise ContractError(
                f"active FO4VR Effect identity changed for 0x{descriptor:08X}"
            )
        originals[descriptor] = by_key[descriptor]
    return originals


def run_fxc(arguments: list[str], label: str) -> None:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        details = (result.stdout + result.stderr).strip()
        raise ContractError(f"fxc failed for {label}: {details}")


def compile_effect_shader(
    fxc: Path,
    source: Path,
    source_directory: Path,
    descriptor: int,
    output: Path,
    assembly: Path,
    label: str,
) -> None:
    run_fxc(
        [
            str(fxc),
            "/nologo",
            "/T",
            "ps_5_0",
            "/E",
            "PSMain",
            "/O3",
            "/Ges",
            "/WX",
            "/D",
            f"EFFECT_TECHNIQUE=0x{descriptor:08X}",
            "/I",
            str(source_directory),
            "/Fo",
            str(output),
            "/Fc",
            str(assembly),
            str(source),
        ],
        label,
    )


def signature_contract(assembly: str) -> str:
    try:
        start = assembly.index("// Input signature:")
        end = assembly.index("ps_5_0", start)
    except ValueError as error:
        raise ContractError("shader assembly is missing signature tables") from error
    return "\n".join(
        line.rstrip()
        for line in assembly[start:end].splitlines()
        if line.startswith("//")
    )


def compile_candidates(
    root: Path,
    manifest: list[dict[str, object]],
    originals: dict[int, census.DxbcContainer],
    contracts: dict[int, tuple[tuple[int, str], tuple[int, ...]]],
    fxc: Path,
    output_directory: Path,
) -> dict[int, bytes]:
    source_directory = (
        root / "package" / "Shaders" / "Community" / "EffectLinearLighting"
    )
    source = source_directory / "EffectLinearLighting.hlsl"
    source_text = source.read_text(encoding="utf-8")
    required_source = (
        '#include "../LinearLighting/LinearLighting.hlsli"',
        "LinearLightingEffectTexture(baseColor.xyz)",
        "LinearLightingEffectMaterialColor(EffectBaseColor.xyz)",
        "LinearLightingEffectMaterialColor(baseColor.xyz)",
        "LinearLightingEffectGeometryColor(",
        "LinearLightingEffectMembraneColor(",
        "LinearLightingEffectMembraneCoordinate(EffectPropertyColor.x)",
        "(EFFECT_TECHNIQUE & 0x1)",
        "(EFFECT_TECHNIQUE & 0x00006004)",
        "(EFFECT_TECHNIQUE & 0x20)",
        "(EFFECT_TECHNIQUE & 0x40)",
        "(EFFECT_TECHNIQUE & 0x1080)",
        "(EFFECT_TECHNIQUE & 0x1000)",
        "(EFFECT_TECHNIQUE & 0x00002000)",
        "(EFFECT_TECHNIQUE & 0x00004000)",
        "EffectGrayscaleTexture.Sample(",
        "EffectGrayscaleSampler,",
        "EffectUnusedPerMaterial.x",
        "(EFFECT_TECHNIQUE & 0x00100000)",
        "(EFFECT_TECHNIQUE & 0x00000400)",
        "(EFFECT_TECHNIQUE & 0x03000000)",
        "(EFFECT_TECHNIQUE & 0x00000010)",
        "(EFFECT_TECHNIQUE & 0x00200000)",
        "baseColor.w *= input.texCoord.z;",
        "baseColor.xyz *= input.texCoord.z;",
        "(EFFECT_TECHNIQUE & 0x00000200)",
        "EffectMembraneRimColor",
        "EffectMembraneVariables",
        "EffectNormalTexture.Sample(",
        "EffectNormalSampler,",
        "input.membraneTangent0",
        "membraneGrayscaleScale",
        "(EFFECT_TECHNIQUE & 0x00080000)",
        "EffectEnvironmentMapScale",
        "EffectEnvironmentTexture.Sample(",
        "EffectEnvironmentSampler,",
        "EffectEnvironmentMaskTexture.Sample(",
        "EffectEnvironmentMaskSampler,",
        "input.environmentViewVector",
        "input.environmentTangent0",
        "environmentNormalSample.w",
        "(EFFECT_TECHNIQUE & 0x00400000)",
        "EffectDistortionScale",
        "EffectDistortionTexture.Sample(",
        "EffectDistortionSampler,",
        "EffectDistortionMaskTexture.Sample(",
        "EffectDistortionMaskSampler,",
        "input.particleData.y",
        "EffectAlphaMaskTexture.Sample(",
        "EffectAlphaMaskSampler,",
        "alphaMask - EffectAlphaTest.x",
        "(EFFECT_TECHNIQUE & 0x00008000)",
        "baseColor.w = 1.0f;",
        "baseColor.xyz *= membraneEffectMult;",
        "(EFFECT_TECHNIQUE & 0x08000000)",
        "(EFFECT_TECHNIQUE & 0x40000000)",
        "EffectUIMaskTechniqueData[rectangleIndex + 2]",
        "LinearLightingEffect(EffectUIMaskTechniqueData[18].xyz)",
        "EffectPointLightPositionX[eyeIndex]",
        "EffectPointLightColorToLinear",
        "effectLightingMult",
        "LinearLightingFog(input.fogParam.xyz)",
        "LinearLightingFogAlpha(input.fogParam.w)",
        "EffectAlphaTest.y - sampledAlpha",
        "lightColor *= otherEffectMult;",
        "LinearLightingEffectAlpha(alpha)",
        "LinearLightingEffectVertexColor(input.vertexColor)",
    )
    for required in required_source:
        if required not in source_text:
            raise ContractError(f"Effect HLSL is missing contract: {required}")
    forbidden_source = (
        "LinearLightingEffect(EffectBaseColor.xyz)",
        "LinearLightingEffect(EffectPropertyColor.xyz)",
        "pow(abs(EffectPropertyColor.x), 1.0f / 2.2f)",
        "baseColor.xyz *= LinearLightingEffectGeometryColor(\n        EffectPropertyColor.xyz);",
        "LinearLightingEffectGeometryCoordinate(EffectPropertyColor.x) *\n        membraneGrayscaleScale",
        "EffectMembraneRimColor * membraneFactor",
    )
    for forbidden in forbidden_source:
        if forbidden in source_text:
            raise ContractError(
                f"Effect HLSL retains stale producer conversion: {forbidden}"
            )
    point_light_start = source_text.index(
        "float4 EffectPointLightColorToLinear"
    )
    point_light_end = source_text.index("float3 EffectLightingColor")
    if "pow(" in source_text[point_light_start:point_light_end]:
        raise ContractError(
            "Effect HLSL decodes producer-space point-light RGB twice"
        )
    if source_text.index("lightColor *= otherEffectMult;") > source_text.index(
        "float3 blendedColor = lerp(lightColor, fogColor, fogFactor);"
    ):
        raise ContractError("Effect multiplier must be applied before fog blending")
    if source_text.index(
        "baseColor.xyz += environmentColor *"
    ) < source_text.index("baseColor.xyz *= input.texCoord.z;"):
        raise ContractError(
            "Effect envmap contribution must be applied after RGB falloff"
        )
    if source_text.index(
        "baseColor.xyz += environmentColor *"
    ) > source_text.index("const float3 propertyColor = EffectLightingColor("):
        raise ContractError(
            "Effect envmap contribution must be applied before lighting influence"
        )

    candidates: dict[int, bytes] = {}
    for entry in manifest:
        descriptor = int(entry["descriptor"])
        name = str(entry["name"])
        original = originals[descriptor]
        candidate_path = output_directory / f"{name}.dxbc"
        candidate_assembly_path = output_directory / f"{name}.asm.txt"
        original_path = output_directory / f"{name}.vanilla.dxbc"
        original_assembly_path = output_directory / f"{name}.vanilla.asm.txt"
        compile_effect_shader(
            fxc,
            source,
            source_directory,
            descriptor,
            candidate_path,
            candidate_assembly_path,
            name,
        )
        candidate_data = candidate_path.read_bytes()
        for alias in contracts[descriptor][1]:
            alias_path = output_directory / f"{name}.alias.{alias:08X}.dxbc"
            alias_assembly_path = (
                output_directory / f"{name}.alias.{alias:08X}.asm.txt"
            )
            compile_effect_shader(
                fxc,
                source,
                source_directory,
                alias,
                alias_path,
                alias_assembly_path,
                f"{name} alias 0x{alias:08X}",
            )
            if alias_path.read_bytes() != candidate_data:
                raise ContractError(
                    f"{name} alias 0x{alias:08X} changed candidate bytecode"
                )
        original_path.write_bytes(original.data)
        run_fxc(
            [
                str(fxc),
                "/nologo",
                "/dumpbin",
                "/Fc",
                str(original_assembly_path),
                str(original_path),
            ],
            f"{name} vanilla",
        )

        candidate_assembly = candidate_assembly_path.read_text(encoding="utf-8")
        original_assembly = original_assembly_path.read_text(encoding="utf-8")
        candidate_signature = signature_contract(candidate_assembly)
        original_signature = signature_contract(original_assembly)
        if candidate_signature != original_signature:
            raise ContractError(
                f"{name} changed the exact FO4VR shader signature:\n"
                f"candidate:\n{candidate_signature}\noriginal:\n{original_signature}"
            )

        candidate_declarations = census.parse_declarations(candidate_assembly)
        original_declarations = census.parse_declarations(original_assembly)
        candidate_buffers = dict(candidate_declarations.constant_buffers)
        original_buffers = dict(original_declarations.constant_buffers)
        # The independently fixed-2.2 membrane producer no longer consumes
        # the general Effect producer exponent in b5[6].w. Membranes end at
        # their multiplier in b5[5], while non-membrane contracts retain the
        # complete seven-register general Effect frame contract.
        expected_frame_buffer_size = 6 if descriptor & 0x00000200 else 7
        if candidate_buffers.get(5) != expected_frame_buffer_size:
            raise ContractError(
                f"{name} does not consume frame-only "
                f"b5[{expected_frame_buffer_size}]"
            )
        if 8 in candidate_buffers:
            raise ContractError(f"{name} unexpectedly consumes geometry b8")
        candidate_buffers.pop(5)
        if candidate_buffers != original_buffers:
            raise ContractError(f"{name} changed vanilla constant buffers")
        if (
            candidate_declarations.samplers != original_declarations.samplers
            or candidate_declarations.textures != original_declarations.textures
        ):
            raise ContractError(f"{name} changed texture/sampler bindings")
        candidates[descriptor] = candidate_data
    return candidates


def format_identity(data: bytes, indent: str) -> list[str]:
    rows = [f"{indent}{{", f"{indent}    {len(data)},", f"{indent}    {{"]
    for offset in range(4, 20, 4):
        values = ", ".join(
            f"std::byte{{ 0x{value:02X} }}" for value in data[offset : offset + 4]
        )
        rows.append(f"{indent}        {values},")
    rows.extend((f"{indent}    }},", f"{indent}}},"))
    return rows


def render_contracts(
    manifest: list[dict[str, object]],
    originals: dict[int, census.DxbcContainer],
    candidates: dict[int, bytes],
) -> str:
    rows = [
        "// Generated by tools/generate_effect_linear_lighting_contracts.py.",
        "// Do not edit this file by hand.",
        f"constexpr std::array<EffectShaderContractDefinition, "
        f"{len(manifest)}> kEffectShaderContracts{{ {{",
    ]
    for entry in manifest:
        descriptor = int(entry["descriptor"])
        rows.extend(
            (
                "    {",
                f'        "{entry["name"]}",',
                f"        {descriptor}u,",
                f'        {entry["resource"]},',
            )
        )
        rows.extend(format_identity(originals[descriptor].data, "        "))
        rows.extend(format_identity(candidates[descriptor], "        "))
        rows.append("    },")
    rows.extend(("} };", ""))
    return "\n".join(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--write-assets", action="store_true")
    arguments = parser.parse_args()
    if arguments.check == arguments.write_assets:
        print("select exactly one of --check or --write-assets", file=sys.stderr)
        return 1

    try:
        root = arguments.root.resolve()
        inventory = census.parse_fxp((root / "Shaders012_VR.fxp").read_bytes())
        contracts = expected_contracts(inventory)
        manifest = read_manifest(root, contracts)
        originals = effect_originals(inventory, contracts)
        fxc = census.find_fxc(None)
        asset_directory = (
            root / "package" / "Shaders" / "Community" / "EffectLinearLighting"
        )
        verified_directory = (
            root
            / "package"
            / "Shaders"
            / "Community"
            / "VerifiedEffectLinearLighting"
        )
        with tempfile.TemporaryDirectory(
            prefix="fo4vr_effect_linear_lighting_"
        ) as temporary:
            candidates = compile_candidates(
                root, manifest, originals, contracts, fxc, Path(temporary)
            )
        generated = render_contracts(manifest, originals, candidates)
        output = arguments.output.resolve()

        if arguments.write_assets:
            asset_directory.mkdir(parents=True, exist_ok=True)
            verified_directory.mkdir(parents=True, exist_ok=True)
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                name = str(entry["name"])
                (asset_directory / f"{name}.dxbc").write_bytes(
                    candidates[descriptor]
                )
                (verified_directory / f"{name}.dxbc").write_bytes(
                    originals[descriptor].data
                )
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated, encoding="utf-8", newline="\n")
        else:
            if not output.is_file() or output.read_text(encoding="utf-8") != generated:
                raise ContractError(f"generated Effect contracts are stale: {output}")
            for entry in manifest:
                descriptor = int(entry["descriptor"])
                name = str(entry["name"])
                candidate_asset = asset_directory / f"{name}.dxbc"
                if (
                    not candidate_asset.is_file()
                    or candidate_asset.read_bytes() != candidates[descriptor]
                ):
                    raise ContractError(
                        f"packaged Effect replacement is stale: {candidate_asset}"
                    )
                verified_asset = verified_directory / f"{name}.dxbc"
                if (
                    not verified_asset.is_file()
                    or verified_asset.read_bytes() != originals[descriptor].data
                ):
                    raise ContractError(
                        f"verified Effect original is stale: {verified_asset}"
                    )
    except (OSError, ContractError, census.CensusError, json.JSONDecodeError) as error:
        print(
            f"Effect Linear Lighting contract generation failed: {error}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
