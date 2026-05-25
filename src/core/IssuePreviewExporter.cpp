#include "IssuePreviewExporter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace gisqc {

namespace {

const char* severityClass(Severity severity) {
    switch (severity) {
    case Severity::Error:
        return "error";
    case Severity::Warning:
        return "warning";
    default:
        return "info";
    }
}

const char* severityText(Severity severity) {
    switch (severity) {
    case Severity::Error:
        return "错误";
    case Severity::Warning:
        return "警告";
    default:
        return "提示";
    }
}

} // namespace

std::string IssuePreviewExporter::toHtml(const std::vector<IssueRecord>& issues, const std::string& title) {
    std::vector<const IssueRecord*> previewIssues;
    for (const auto& issue : issues) {
        if (issue.hasPreviewGeometry) {
            previewIssues.push_back(&issue);
        }
    }

    std::ostringstream out;
    out << "<!doctype html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "<title>" << escapeHtml(title) << "</title>\n"
        << "<style>\n"
        << "body{margin:0;font-family:'Microsoft YaHei',Segoe UI,Arial,sans-serif;background:#f5f7fb;color:#172033;}"
        << ".app{display:grid;grid-template-columns:360px 1fr;height:100vh;}"
        << ".side{background:#fff;border-right:1px solid #dfe7f2;overflow:auto;}"
        << ".head{padding:16px 18px;border-bottom:1px solid #edf1f7;}h1{font-size:18px;margin:0 0 6px;}"
        << ".meta{color:#667085;font-size:13px}.item{padding:12px 16px;border-bottom:1px solid #edf1f7;cursor:pointer;}"
        << ".item:hover,.item.active{background:#eef6ff}.code{font-weight:700;color:#0b5cad}.fid{color:#475467;font-size:12px;margin-top:3px}.desc{font-size:13px;margin-top:6px;line-height:1.45;}"
        << ".badge{display:inline-block;border-radius:999px;padding:2px 8px;font-size:12px;margin-left:6px}.error{background:#fee4e2;color:#b42318}.warning{background:#fff2cc;color:#946200}.info{background:#e0f2fe;color:#026aa2}"
        << ".mapWrap{position:relative;background:#e8eef7;height:100vh;overflow:hidden;}svg{width:100%;height:100%;display:block;background:linear-gradient(45deg,#eef3f9,#e3ebf5);}"
        << ".grid{stroke:#cbd5e1;stroke-width:0.4;opacity:.55}.geom{fill:rgba(239,68,68,.20);stroke:#ef4444;stroke-width:2;vector-effect:non-scaling-stroke}.geom.point{fill:#ef4444}.geom.active{stroke:#2563eb;fill:rgba(37,99,235,.20);stroke-width:3}.label{font-size:12px;paint-order:stroke;stroke:#fff;stroke-width:3;fill:#111827}.empty{padding:32px;color:#667085}.toolbar{position:absolute;left:16px;top:16px;background:rgba(255,255,255,.94);border:1px solid #d0d7e2;border-radius:10px;padding:10px 12px;box-shadow:0 8px 24px rgba(15,23,42,.12);font-size:13px}.hint{position:absolute;right:16px;bottom:16px;background:rgba(255,255,255,.94);border-radius:10px;padding:10px 12px;color:#475467;font-size:12px}"
        << "</style>\n</head>\n<body>\n<div class=\"app\">\n<aside class=\"side\">\n<div class=\"head\"><h1>" << escapeHtml(title) << "</h1><div class=\"meta\">可预览问题 " << previewIssues.size() << " / 总问题 " << issues.size() << "</div></div>\n";

    if (previewIssues.empty()) {
        out << "<div class=\"empty\">当前问题清单没有可定位几何。目录、文件、字段类问题仍在报告/CSV 中查看；空间图斑类问题会在这里显示范围和定位。</div>\n";
    } else {
        for (std::size_t i = 0; i < previewIssues.size(); ++i) {
            const auto& issue = *previewIssues[i];
            out << "<div class=\"item" << (i == 0 ? " active" : "") << "\" data-idx=\"" << i << "\">"
                << "<div><span class=\"code\">" << escapeHtml(issue.ruleCode) << "</span>"
                << "<span class=\"badge " << severityClass(issue.severity) << "\">" << severityText(issue.severity) << "</span></div>"
                << "<div class=\"fid\">图层：" << escapeHtml(issue.layerName) << " / 要素：" << escapeHtml(issue.featureId) << "</div>"
                << "<div class=\"desc\">" << escapeHtml(issue.description) << "</div></div>\n";
        }
    }

    out << "</aside><main class=\"mapWrap\"><div class=\"toolbar\"><b>问题图斑预览</b><br>点击左侧问题可定位；滚轮缩放浏览器页面。</div><svg id=\"map\" viewBox=\"0 0 1000 700\" preserveAspectRatio=\"xMidYMid meet\"></svg><div class=\"hint\">红色为问题图斑/定位点，蓝色为当前选中问题</div></main></div>\n";

    out << "<script>\nconst issues=[\n";
    out << std::fixed << std::setprecision(8);
    for (std::size_t i = 0; i < previewIssues.size(); ++i) {
        const auto& issue = *previewIssues[i];
        out << "{id:'" << escapeJs(issue.issueId) << "',code:'" << escapeJs(issue.ruleCode)
            << "',layer:'" << escapeJs(issue.layerName) << "',fid:'" << escapeJs(issue.featureId)
            << "',desc:'" << escapeJs(issue.description) << "',wkt:'" << escapeJs(issue.geometryWkt)
            << "',x:" << issue.previewX << ",y:" << issue.previewY << "}";
        if (i + 1 < previewIssues.size()) out << ',';
        out << "\n";
    }
    out << "];\n"
        << R"JS(
const svg=document.getElementById('map');
const NS='http://www.w3.org/2000/svg';
function nums(s){return (s.match(/-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/ig)||[]).map(Number)}
function geomPoints(wkt){
  const n=nums(wkt); const pts=[];
  for(let i=0;i+1<n.length;i+=2) pts.push([n[i],n[i+1]]);
  return pts;
}
let all=[]; issues.forEach(it=>{it.pts=geomPoints(it.wkt); if(!it.pts.length) it.pts=[[it.x,it.y]]; all.push(...it.pts)});
let minX=0,minY=0,maxX=1,maxY=1;
if(all.length){minX=Math.min(...all.map(p=>p[0]));maxX=Math.max(...all.map(p=>p[0]));minY=Math.min(...all.map(p=>p[1]));maxY=Math.max(...all.map(p=>p[1]));}
if(maxX-minX<1e-9){minX-=1;maxX+=1} if(maxY-minY<1e-9){minY-=1;maxY+=1}
const pad=60,w=1000,h=700,sx=(w-pad*2)/(maxX-minX),sy=(h-pad*2)/(maxY-minY),s=Math.min(sx,sy);
function X(x){return pad+(x-minX)*s+(w-pad*2-(maxX-minX)*s)/2}
function Y(y){return h-pad-(y-minY)*s-(h-pad*2-(maxY-minY)*s)/2}
function add(tag,attrs){const e=document.createElementNS(NS,tag);Object.entries(attrs).forEach(([k,v])=>e.setAttribute(k,v));svg.appendChild(e);return e}
for(let i=0;i<=10;i++){add('line',{class:'grid',x1:pad+i*(w-2*pad)/10,y1:pad,x2:pad+i*(w-2*pad)/10,y2:h-pad});add('line',{class:'grid',x1:pad,y1:pad+i*(h-2*pad)/10,x2:w-pad,y2:pad+i*(h-2*pad)/10});}
function drawIssue(it,idx){
  const wkt=it.wkt.toUpperCase(); let el;
  if(wkt.startsWith('POINT')){el=add('circle',{class:'geom point',cx:X(it.pts[0][0]),cy:Y(it.pts[0][1]),r:5});}
  else if(wkt.includes('POLYGON')){el=add('polygon',{class:'geom',points:it.pts.map(p=>`${X(p[0])},${Y(p[1])}`).join(' ')});}
  else {el=add('polyline',{class:'geom',fill:'none',points:it.pts.map(p=>`${X(p[0])},${Y(p[1])}`).join(' ')});}
  el.dataset.idx=idx; el.addEventListener('click',()=>select(idx));
  add('text',{class:'label',x:X(it.pts[0][0])+8,y:Y(it.pts[0][1])-8}).textContent=it.code+' #'+it.fid;
}
issues.forEach(drawIssue);
function select(idx){
  document.querySelectorAll('.item').forEach((e,i)=>e.classList.toggle('active',i===idx));
  svg.querySelectorAll('.geom').forEach(e=>e.classList.toggle('active',Number(e.dataset.idx)===idx));
}
document.querySelectorAll('.item').forEach((e,i)=>e.addEventListener('click',()=>select(i)));
if(issues.length) select(0);
)JS";
    out << "</script>\n</body>\n</html>\n";
    return out.str();
}

std::string IssuePreviewExporter::escapeHtml(const std::string& value) {
    std::string out;
    for (const char ch : value) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string IssuePreviewExporter::escapeJs(const std::string& value) {
    std::string out;
    for (const char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '\'': out += "\\'"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        case '<': out += "\\x3C"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

} // namespace gisqc
