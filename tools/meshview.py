"""Write an HTML page that draws a pctest mesh with WebGL, mimicking the 3DS renderer."""
import sys, base64
data = open(sys.argv[1], 'rb').read()
yaw = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
pitch = float(sys.argv[4]) if len(sys.argv) > 4 else 0.95
dist = float(sys.argv[5]) if len(sys.argv) > 5 else 230
html = '''<!doctype html><html><body style="margin:0;background:#111"><canvas id=c width=800 height=480></canvas><script>
const raw=Uint8Array.from(atob("%s"),c=>c.charCodeAt(0)).buffer;
const H=new Int32Array(raw,0,4),NV=H[0],NS=H[1],CX=H[2],CZ=H[3];
const V=new Float32Array(raw,16,NV*6),S=new Float32Array(raw,16+NV*24,NS*6);
const A=new Uint8Array(raw,16+(NV+NS)*24,128*128*4*2);
const gl=document.getElementById('c').getContext('webgl');
const vs=`attribute vec3 p;attribute vec2 t;attribute float s;uniform mat4 M;varying vec2 vt;varying float vs;void main(){gl_Position=M*vec4(p,1.);vt=t;vs=s;}`;
const fs=`precision mediump float;uniform sampler2D T;varying vec2 vt;varying float vs;void main(){vec4 c=texture2D(T,vt);if(c.a<.5)discard;gl_FragColor=vec4(c.rgb*vs,1.);}`;
function sh(t,s){const x=gl.createShader(t);gl.shaderSource(x,s);gl.compileShader(x);return x}
const pr=gl.createProgram();gl.attachShader(pr,sh(gl.VERTEX_SHADER,vs));gl.attachShader(pr,sh(gl.FRAGMENT_SHADER,fs));gl.linkProgram(pr);gl.useProgram(pr);
function tex(off){const t=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,t);
 const px=new Uint8Array(128*128*4);for(let i=0;i<128*128;i++){const o=off+i*4;px[i*4]=A[o+3];px[i*4+1]=A[o+2];px[i*4+2]=A[o+1];px[i*4+3]=A[o];}
 gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,128,128,0,gl.RGBA,gl.UNSIGNED_BYTE,px);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);return t}
const tb=tex(0),to=tex(128*128*4);
function buf(a){const b=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,b);gl.bufferData(gl.ARRAY_BUFFER,a,gl.STATIC_DRAW);return b}
const bv=buf(V),bs=buf(S);
const lp=gl.getAttribLocation(pr,'p'),lt=gl.getAttribLocation(pr,'t'),ls=gl.getAttribLocation(pr,'s');
function bind(b){gl.bindBuffer(gl.ARRAY_BUFFER,b);gl.enableVertexAttribArray(lp);gl.vertexAttribPointer(lp,3,gl.FLOAT,false,24,0);gl.enableVertexAttribArray(lt);gl.vertexAttribPointer(lt,2,gl.FLOAT,false,24,12);gl.enableVertexAttribArray(ls);gl.vertexAttribPointer(ls,1,gl.FLOAT,false,24,20)}
const sub=(a,b)=>a.map((v,i)=>v-b[i]),dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2],cr=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]],nm=a=>{const l=Math.hypot(...a);return a.map(v=>v/l)};
function mul(a,b){const r=new Array(16).fill(0);for(let i=0;i<4;i++)for(let j=0;j<4;j++)for(let k=0;k<4;k++)r[j*4+i]+=a[k*4+i]*b[j*4+k];return r}
const yaw=%f,pitch=%f,dist=%f,tg=[CX+8,8,CZ+8];
const e=[tg[0]+Math.sin(yaw)*Math.cos(pitch)*dist,tg[1]+Math.sin(pitch)*dist,tg[2]+Math.cos(yaw)*Math.cos(pitch)*dist];
const z=nm(sub(e,tg)),x=nm(cr([0,1,0],z)),y=cr(z,x);
const L=[x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,-dot(x,e),-dot(y,e),-dot(z,e),1];
const f=1/Math.tan(0.75/2),a=800/480,n=4,fr=3000,P=[f/a,0,0,0,0,f,0,0,0,0,(fr+n)/(n-fr),-1,0,0,2*fr*n/(n-fr),0];
gl.uniformMatrix4fv(gl.getUniformLocation(pr,'M'),false,new Float32Array(mul(P,L)));
gl.enable(gl.DEPTH_TEST);gl.clearColor(.45,.7,.85,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
gl.bindTexture(gl.TEXTURE_2D,tb);bind(bv);gl.drawArrays(gl.TRIANGLES,0,NV);
gl.bindTexture(gl.TEXTURE_2D,to);bind(bs);gl.drawArrays(gl.TRIANGLES,0,NS);
</script></body></html>''' % (base64.b64encode(data).decode(), yaw, pitch, dist)
open(sys.argv[2], 'w').write(html)
