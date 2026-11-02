"""Draw a pctest output with WebGL the way the 3DS renderer does (same camera, lighting
and shadow pass), for checking on a PC. Usage: meshview.py mesh.bin out.html [pitch dist]"""
import sys, base64
data = open(sys.argv[1], 'rb').read()
pitch = float(sys.argv[3]) if len(sys.argv) > 3 else 0.72
dist = float(sys.argv[4]) if len(sys.argv) > 4 else 215
html = r'''<!doctype html><html><body style="margin:0;background:#111"><canvas id=c width=800 height=480></canvas><script>
const raw=Uint8Array.from(atob("DATA"),c=>c.charCodeAt(0)).buffer;
const H=new Int32Array(raw,0,6),NV=H[0],NS=H[1],NH=H[2],CX=H[3],CZ=H[4],SKY=H[5]>>>0;
const LU=new Float32Array(raw,24,6);
const off=48,V=new Float32Array(raw,off,NV*7),S=new Float32Array(raw,off+NV*28,NS*7),SH=new Float32Array(raw,off+(NV+NS)*28,NH*7);
const A=new Uint8Array(raw,off+(NV+NS+NH)*28,128*128*4*2);
let q=off+(NV+NS+NH)*28+131072;const CAM=new Float32Array(raw.slice(q,q+20));q+=20;const BN=new Int32Array(raw.slice(q,q+8));q+=8;
const BQ=new Float32Array(raw.slice(q,q+BN[0]*28));q+=BN[0]*28;const BS=new Float32Array(raw.slice(q,q+BN[1]*28));q+=BN[1]*28;
const BT=new Uint8Array(raw.slice(q,q+128*64*4));
const gl=document.getElementById('c').getContext('webgl',{antialias:false});
const vs=`attribute vec3 p;attribute vec2 t;attribute vec2 l;uniform mat4 M;uniform vec3 amb,glw;varying vec2 vt;varying vec3 vc;
void main(){gl_Position=M*vec4(p,1.);vt=t;vc=min(vec3(1.),amb*l.x+glw*l.y);}`;
const fs=`precision mediump float;uniform sampler2D T;uniform float blob;varying vec2 vt;varying vec3 vc;
void main(){if(blob>0.5){vec2 d=(vt-.5)*2.;float r=length(d);gl_FragColor=vec4(0.,0.,0.,r>=1.?0.:(1.-r*r)*.55);return;}
vec4 c=texture2D(T,vt);if(c.a<.5)discard;gl_FragColor=vec4(c.rgb*vc,1.);}`;
function sh(t,s){const x=gl.createShader(t);gl.shaderSource(x,s);gl.compileShader(x);if(!gl.getShaderParameter(x,gl.COMPILE_STATUS))document.title=gl.getShaderInfoLog(x);return x}
const pr=gl.createProgram();gl.attachShader(pr,sh(gl.VERTEX_SHADER,vs));gl.attachShader(pr,sh(gl.FRAGMENT_SHADER,fs));gl.linkProgram(pr);gl.useProgram(pr);
function tex(o,S,w,h){S=S||A;w=w||128;h=h||128;const t=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,t);const px=new Uint8Array(w*h*4);
 for(let i=0;i<w*h;i++){const q=o+i*4;px[i*4]=S[q+3];px[i*4+1]=S[q+2];px[i*4+2]=S[q+1];px[i*4+3]=S[q];}
 gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,w,h,0,gl.RGBA,gl.UNSIGNED_BYTE,px);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);return t}
const tb=tex(0),to=tex(65536),tt=tex(0,BT,128,64);
function buf(a){const b=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,b);gl.bufferData(gl.ARRAY_BUFFER,a,gl.STATIC_DRAW);return b}
const lp=gl.getAttribLocation(pr,'p'),lt=gl.getAttribLocation(pr,'t'),ll=gl.getAttribLocation(pr,'l');
function bind(b){gl.bindBuffer(gl.ARRAY_BUFFER,b);[[lp,3,0],[lt,2,12],[ll,2,20]].forEach(([l,n,o])=>{gl.enableVertexAttribArray(l);gl.vertexAttribPointer(l,n,gl.FLOAT,false,28,o)})}
const sub=(a,b)=>a.map((v,i)=>v-b[i]),dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2],cr=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]],nm=a=>{const l=Math.hypot(...a);return a.map(v=>v/l)};
function mul(a,b){const r=new Array(16).fill(0);for(let i=0;i<4;i++)for(let j=0;j<4;j++)for(let k=0;k<4;k++)r[j*4+i]+=a[k*4+i]*b[j*4+k];return r}
const pitch=CAM[3],dist=CAM[4],yaw=CAM[2],tg=[CAM[0],6,CAM[1]],e=[tg[0]+Math.sin(yaw)*Math.cos(pitch)*dist,tg[1]+Math.sin(pitch)*dist,tg[2]+Math.cos(yaw)*Math.cos(pitch)*dist];
const z=nm(sub(e,tg)),x=nm(cr([0,1,0],z)),y=cr(z,x);
const L=[x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,-dot(x,e),-dot(y,e),-dot(z,e),1];
const f=1/Math.tan(42*Math.PI/360),a=400/240,n=6,fr=1600,P=[f/a,0,0,0,0,f,0,0,0,0,(fr+n)/(n-fr),-1,0,0,2*fr*n/(n-fr),0];
gl.uniformMatrix4fv(gl.getUniformLocation(pr,'M'),false,new Float32Array(mul(P,L)));
gl.uniform3f(gl.getUniformLocation(pr,'amb'),LU[0],LU[1],LU[2]);gl.uniform3f(gl.getUniformLocation(pr,'glw'),LU[3],LU[4],LU[5]);
const ub=gl.getUniformLocation(pr,'blob');
gl.enable(gl.DEPTH_TEST);gl.clearColor((SKY>>>24)/255,((SKY>>>16)&255)/255,((SKY>>>8)&255)/255,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
gl.uniform1f(ub,0);gl.bindTexture(gl.TEXTURE_2D,tb);bind(buf(V));gl.drawArrays(gl.TRIANGLES,0,NV);
gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);gl.depthMask(false);gl.uniform1f(ub,1);bind(buf(SH));gl.drawArrays(gl.TRIANGLES,0,NH);
if(BN[1]){bind(buf(BS));gl.drawArrays(gl.TRIANGLES,0,BN[1]);}
gl.depthMask(true);gl.uniform1f(ub,0);gl.bindTexture(gl.TEXTURE_2D,to);bind(buf(S));gl.drawArrays(gl.TRIANGLES,0,NS);
if(BN[0]){gl.bindTexture(gl.TEXTURE_2D,tt);bind(buf(BQ));gl.drawArrays(gl.TRIANGLES,0,BN[0]);}
</script></body></html>'''
html = html.replace('DATA', base64.b64encode(data).decode()).replace('PITCH', str(pitch)).replace('DIST', str(dist))
open(sys.argv[2], 'w').write(html)
