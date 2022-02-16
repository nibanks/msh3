# msh3

Minimal HTTP/3 client on top of [microsoft/msquic](https://github.com/microsoft/msquic) and [litespeedtech/ls-qpack](https://github.com/litespeedtech/ls-qpack).

# Build

```
git clone https://github.com/nibanks/msh3
git submodule update --init --recursive
cd msh3
mkdir build && cd build
```

## Linux
```
cmake -G 'Unix Makefiles' ..
cmake --build .
```

## Windows
```
cmake -G 'Visual Studio 17 2022' -A x64 ..
cmake --build .
```

# Run

```
msh3app www.cloudflare.com
msh3app www.google.com /index.html
```

## Sample output

```
$ /workspaces/msh3/bld/tool/msh3app www.google.com
HTTP/3 GET https://www.google.com/

:status:200
date:Wed, 16 Feb 2022 22:02:04 GMT
expires:-1
cache-control:private, max-age=0
content-type:text/html; charset=ISO-8859-1
p3p:CP="This is not a P3P policy! See g.co/p3phelp for more info."
server:gws
x-xss-protection:0
x-frame-options:SAMEORIGIN
set-cookie:1P_JAR=2022-02-16-22; expires=Fri, 18-Mar-2022 22:02:04 GMT; path=/; domain=.google.com; Secure
set-cookie:NID=511=SE0721Ls9fnE9BkL7k_GBwC718boRyiu8WJT4A-YVhZUkmaAFG7JCGzy2b1sSduN1OFyHynSY-4O70VRVS7-61SFmAyn8GMhx_3HMFXoC-J7rHUTIpS0YyRZlKptiv-0TML6ieKaonrhRiPjwb6kWKvTaO4yT5KkSdTDWh5T0Ck; expires=Thu, 18-Aug-2022 22:02:04 GMT; path=/; domain=.google.com; HttpOnly
accept-ranges:none
vary:Accept-Encoding
<!doctype html><html itemscope="" itemtype="http://schema.org/WebPage" lang="en"><head><meta content="Search the world's information, including webpages, images, videos and more. Google has many special features to help you find exactly what you're looking for." name="description"><meta content="noodp" name="robots"><meta content="text/html; charset=UTF-8" http-equiv="Content-Type"><meta content="/images/branding/googleg/1x/googleg_standard_color_128dp.png" itemprop="image"><title>Google</title><script nonce="4Jz7JBPoeh/NW+CL4TF8uQ==">(function(){window.google={kEI:'XHQNYuPwLZXBytMPxNSsuAw',kEXPI:'0,1302536,56873,6058,207,2415,2389,2316,383,246,5,1354,4013,923,314,1122516,1197730,671,22,380068,16114,17444,11240,17572,4859,1361,9290,3024,2821,1930,12834,4020,978,13228,3847,10622,7432,15309,5081,1593,1279,2742,149,1103,840,1983,4314,108,3391,15,606,2023,1777,520,7885,6785,3227,1989,856,7,5599,11851,7539,6857,1924,908,2,941,6398,8926,432,3,1590,1,5445,148,11323,991,1661,4,1528,2304,7039,17820,2489,1714,3050,2658,7355,32,13628,2305,675,16808,1435,5821,2536,4094,4052,3,3541,1,14711,2096,10375,11207,653,1,3111,2,14022,1931,442,342,255,2993,286,1271,744,5852,9321,1142,1160,5679,1021,2380,2719,16547,1695,1,8,7772,4568,2577,3238,437,3014,3715,1,15657,1037,1252,5835,9212,5756,1538,2794,2204,2083,1745,57,59,1336,445,2,2,1,5663,731,4395,1709,292,4531,300,1,2,3822,3180,1559,10,1,436,665,2,380,7108,113,625,5,1823,3071,945,568,69,161,1,2,3,126,331,2,1660,2,678,850,2916,4322,880,2158,750,39,934,590,677,139,11,1235,1948,177,1591,9,2,55,13,286,1034,811,5,855,148,74,418,596,954,47,3,877,1135,75,595,3,524,116,195,659,372,1414,628,246,370,563,484,1557,934,2,417,5478351,859,5995935,532,2800685,882,444,1,2,80,1,1796,1,2562,1,748,141,795,563,1,4265,1,1,2,1331,4142,2609,155,17,13,72,139,4,2,20,2,169,13,19,46,5,39,96,548,29,2,2,1,2,1,2,2,7,4,1,2,2,2,2,2,2,353,513,186,1,1,158,3,2,2,2,2,2,4,2,3,3,269,1601,141,394,183,61,64,13,1,69,4,30,13,54,5,1,7454045,13278023,3220020,2862027,1179663,3,450,2971,484,9,1435,159,1358,965,3761,3,923,322,20,908,489,1799,1048,681,60,1526',kBL:'bMqx'};google.sn='webhp';google.kHL='en';})();(function(){
var f=this||self;var h,k=[];function l(a){for(var b;a&&(!a.getAttribute||!(b=a.getAttribute("eid")));)a=a.parentNode;return b||h}function m(a){for(var b=null;a&&(!a.getAttribute||!(b=a.getAttribute("leid")));)a=a.parentNode;return b}
function n(a,b,c,d,g){var e="";c||-1!==b.search("&ei=")||(e="&ei="+l(d),-1===b.search("&lei=")&&(d=m(d))&&(e+="&lei="+d));d="";!c&&f._cshid&&-1===b.search("&cshid=")&&"slh"!==a&&(d="&cshid="+f._cshid);c=c||"/"+(g||"gen_204")+"?atyp=i&ct="+a+"&cad="+b+e+"&zx="+Date.now()+d;/^http:/i.test(c)&&"https:"===window.location.protocol&&(google.ml&&google.ml(Error("a"),!1,{src:c,glmm:1}),c="");return c};h=google.kEI;google.getEI=l;google.getLEI=m;google.ml=function(){return null};google.log=function(a,b,c,d,g){if(c=n(a,b,c,d,g)){a=new Image;var e=k.length;k[e]=a;a.onerror=a.onload=a.onabort=function(){delete k[e]};a.src=c}};google.logUrl=n;}).call(this);(function(){
...
```
