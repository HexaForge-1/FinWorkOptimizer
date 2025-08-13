#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <map>
#include <cstdlib>

#ifdef FWO_WITH_SQLITE
#include <sqlite3.h>
#endif

#include "Payroll.h"
#include "RestServer.h"

static std::string API_KEY = "changeme"; // override via env FWO_API_KEY

static std::string http_ok(const std::string& body, const std::string& type="application/json"){
    return "HTTP/1.1 200 OK\r\nContent-Type: "+type+"\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
static std::string http_created(){
    std::string body = "{\"status\":\"created\"}";
    return "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
static std::string http_bad(const std::string& msg){
    std::string body = std::string("{\"error\":\"")+msg+"\"}";
    return "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}
static std::string http_forbidden(){ std::string b="{\"error\":\"forbidden\"}"; return "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(b.size())+"\r\n\r\n"+b; }
static std::string http_notfound(){ std::string b="{\"error\":\"not found\"}"; return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(b.size())+"\r\n\r\n"+b; }

static std::string url_path(const std::string& req){
    auto sp = req.find(" ");
    auto qpos = req.find(" HTTP/1.1");
    if (sp==std::string::npos || qpos==std::string::npos) return "/";
    return req.substr(sp+1, qpos-(sp+1));
}
static std::string url_param(const std::string& req, const std::string& key){
    std::string path = url_path(req);
    auto qp = path.find("?"); if(qp==std::string::npos) return "";
    std::string qs = path.substr(qp+1);
    std::stringstream ss(qs); std::string kv;
    while(std::getline(ss, kv, '&')){
        auto eq = kv.find("=");
        if(eq!=std::string::npos){
            if(kv.substr(0,eq)==key) return kv.substr(eq+1);
        }
    }
    return "";
}
static std::string header_value(const std::string& req, const std::string& header){
    auto pos = req.find(header);
    if(pos==std::string::npos) return "";
    auto end = req.find("\r\n", pos);
    auto line = req.substr(pos, end-pos);
    auto colon = line.find(":");
    if(colon==std::string::npos) return "";
    std::string v = line.substr(colon+1);
    size_t s=0; while(s<v.size() && (v[s]==' ')) s++; return v.substr(s);
}
static std::string body_of(const std::string& req){
    auto p = req.find("\r\n\r\n");
    return (p==std::string::npos) ? "" : req.substr(p+4);
}
static std::string json_get_string(const std::string& body, const std::string& field){
    std::string key = "\""+field+"\"";
    auto p = body.find(key);
    if(p==std::string::npos) return "";
    p = body.find(":", p);
    if(p==std::string::npos) return "";
    p++;
    while(p<body.size() && (body[p]==' ')) p++;
    if(p<body.size() && body[p]=='\"'){
        p++; auto q = body.find("\"", p);
        if(q==std::string::npos) return "";
        return body.substr(p, q-p);
    } else {
        auto q = body.find_first_of(",}\r\n", p);
        return body.substr(p, q-p);
    }
}

struct Emp { std::string id,name,role; };
struct TS { std::string emp, project; double hours; bool approved; std::string date; };
struct EXP { std::string category; double amount; };
struct Budget { std::string category; double amount; };
struct Customer { std::string id,name,segment; };

static std::vector<Emp> MEM_EMP = { {"E1","Alice","Analyst"}, {"E2","Bob","Engineer"} };
static std::vector<TS>  MEM_TS  = { };
static std::vector<EXP> MEM_EXP = { {"Travel",200.0} };
static std::vector<Budget> MEM_BUD = {};
static std::vector<Customer> MEM_CUST = { {"CUST-1","Acme","Enterprise"} };

#ifdef FWO_WITH_SQLITE
static sqlite3* DB = nullptr;
#endif

static std::map<std::string,int> RATE;
static bool allowed(const std::string& ip){
    int& c = RATE[ip];
    c++;
    return c <= 100;
}

static double sum_expense(const std::string& category){
    double s=0.0;
    for(const auto& e: MEM_EXP){
        if(category.empty() || e.category==category) s+=e.amount;
    }
    return s;
}
static bool approve_timesheet_row(const std::string& emp, const std::string& project, const std::string& date){
#ifdef FWO_WITH_SQLITE
    if(DB){
        std::string sql = "UPDATE timesheets SET approved=1 WHERE emp='"+emp+"' AND project='"+project+"'"+(date.empty()?"":" AND date='"+date+"'")+";";
        sqlite3_exec(DB, sql.c_str(), nullptr, nullptr, nullptr);
        return true;
    }
#endif
    bool changed=false;
    for(auto& t: MEM_TS){
        if(t.emp==emp && t.project==project && (date.empty() || t.date==date)){
            t.approved = true; changed=true;
        }
    }
    return changed;
}

int run_rest_server(int port, const std::string& apiKeyEnvVar){
    const char* e = getenv(apiKeyEnvVar.c_str());
    if(e && *e) API_KEY = e;

#ifdef FWO_WITH_SQLITE
    if(sqlite3_open("finwork.db", &DB) != SQLITE_OK){
        std::cerr << "sqlite open failed, continuing with in-memory\\n";
        DB = nullptr;
    } else {
        sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS employees(id TEXT PRIMARY KEY, name TEXT, role TEXT);", nullptr, nullptr, nullptr);
        sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS timesheets(id INTEGER PRIMARY KEY AUTOINCREMENT, emp TEXT, project TEXT, hours REAL, approved INT, date TEXT);", nullptr, nullptr, nullptr);
        sqlite3_exec(DB, "CREATE TABLE IF NOT EXISTS expenses(id INTEGER PRIMARY KEY AUTOINCREMENT, category TEXT, amount REAL);", nullptr, nullptr, nullptr);
    }
#endif

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1){ perror("socket"); return 1; }
    int opt=1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    if(bind(server_fd, (sockaddr*)&addr, sizeof(addr))<0){ perror("bind"); return 1; }
    if(listen(server_fd, 64) < 0){ perror("listen"); return 1; }
    std::cout << "REST server on http://0.0.0.0:" << port << std::endl;

    while(true){
        sockaddr_in caddr{}; socklen_t clen=sizeof(caddr);
        int sock = accept(server_fd, (sockaddr*)&caddr, &clen);
        if(sock<0){ perror("accept"); continue; }

        char buffer[16384]; int n = read(sock, buffer, sizeof(buffer)-1);
        if(n <= 0){ close(sock); continue; }
        buffer[n] = 0;
        std::string req(buffer);

        std::string ip = std::to_string((caddr.sin_addr.s_addr)&0xFF)+"."+
                         std::to_string((caddr.sin_addr.s_addr>>8)&0xFF)+"."+
                         std::to_string((caddr.sin_addr.s_addr>>16)&0xFF)+"."+
                         std::to_string((caddr.sin_addr.s_addr>>24)&0xFF);
        if(!allowed(ip)){ std::string r = http_bad("rate limit"); send(sock, r.c_str(), r.size(), 0); close(sock); continue; }

        std::string headerKey = header_value(req, "X-API-Key");
        std::string queryKey = url_param(req, "key");
        if(API_KEY != "none" && !(headerKey==API_KEY || queryKey==API_KEY)){
            if(req.find("GET /health")==std::string::npos){
                std::string r = http_forbidden(); send(sock, r.c_str(), r.size(), 0); close(sock); continue;
            }
        }

        std::string resp;
        bool is_get = req.rfind("GET ", 0)==0;
        bool is_post = req.rfind("POST ", 0)==0;

        if(is_get && req.find("GET /health")==0){
            resp = http_ok("{\"status\":\"ok\"}");
        }
        else if(is_get && req.find("GET /employees")==0){
#ifdef FWO_WITH_SQLITE
            if(DB){
                std::string rows; char* err=nullptr;
                auto cb = [](void* ctx, int argc, char** argv, char** col)->int{
                    std::string* out = reinterpret_cast<std::string*>(ctx);
                    if(!out->empty()) *out += ",";
                    *out += "{\"id\":\""+std::string(argv[0]?argv[0]:"")+"\",\"name\":\""+std::string(argv[1]?argv[1]:"")+"\",\"role\":\""+std::string(argv[2]?argv[2]:"")+"\"}";
                    return 0;
                };
                sqlite3_exec(DB, "SELECT id,name,role FROM employees;", cb, &rows, &err);
                resp = http_ok("["+rows+"]");
            } else
#endif
            {
                std::string body="[";
                for(size_t i=0;i<MEM_EMP.size();++i){
                    auto& e=MEM_EMP[i];
                    body += "{\"id\":\""+e.id+"\",\"name\":\""+e.name+"\",\"role\":\""+e.role+"\"}";
                    if(i+1<MEM_EMP.size()) body += ",";
                }
                body += "]";
                resp = http_ok(body);
            }
        }
        else if(is_post && req.find("POST /employees")==0){
            std::string b = body_of(req);
            std::string id = json_get_string(b,"id");
            std::string name = json_get_string(b,"name");
            std::string role = json_get_string(b,"role");
            if(id.empty()||name.empty()){ resp = http_bad("invalid employee"); }
            else {
#ifdef FWO_WITH_SQLITE
                if(DB){
                    std::string sql = "INSERT OR REPLACE INTO employees VALUES('"+id+"','"+name+"','"+role+"');";
                    sqlite3_exec(DB, sql.c_str(), nullptr, nullptr, nullptr);
                } else
#endif
                { MEM_EMP.push_back({id,name,role}); }
                resp = http_created();
            }
        }
        else if(is_get && req.find("GET /timesheets")==0){
            std::string emp = url_param(req, "emp");
#ifdef FWO_WITH_SQLITE
            if(DB){
                std::string rows; char* err=nullptr;
                auto cb = [](void* ctx, int argc, char** argv, char** col)->int{
                    std::string* out = reinterpret_cast<std::string*>(ctx);
                    if(!out->empty()) *out += ",";
                    *out += "{\"emp\":\""+std::string(argv[0]?argv[0]:"")+"\",\"project\":\""+std::string(argv[1]?argv[1]:"")+"\",\"hours\":"+std::string(argv[2]?argv[2]:"0")+
                            ",\"approved\":"+std::string(argv[3]?argv[3]:"0")+",\"date\":\""+std::string(argv[4]?argv[4]:"")+"\"}";
                    return 0;
                };
                std::string sql = emp.empty()? "SELECT emp,project,hours,approved,date FROM timesheets;"
                                             : "SELECT emp,project,hours,approved,date FROM timesheets WHERE emp='"+emp+"';";
                sqlite3_exec(DB, sql.c_str(), cb, &rows, &err);
                resp = http_ok("["+rows+"]");
            } else
#endif
            {
                std::string a="["; bool first=true;
                for(const auto& t: MEM_TS){
                    if(!emp.empty() && t.emp!=emp) continue;
                    if(!first) a+=","; first=false;
                    a += "{\"emp\":\""+t.emp+"\",\"project\":\""+t.project+"\",\"hours\":"+std::to_string(t.hours)+",\"approved\":"+(t.approved?"true":"false")+",\"date\":\""+t.date+"\"}";
                }
                a += "]"; resp = http_ok(a);
            }
        }
        else if(is_post && req.find("POST /timesheets")==0){
            std::string b = body_of(req);
            std::string emp = json_get_string(b,"emp");
            std::string project = json_get_string(b,"project");
            std::string hoursS = json_get_string(b,"hours");
            std::string approvedS = json_get_string(b,"approved");
            std::string date = json_get_string(b,"date");
            if(emp.empty()||project.empty()||hoursS.empty()){ resp = http_bad("invalid timesheet"); }
            else {
#ifdef FWO_WITH_SQLITE
                if(DB){
                    std::string sql = "INSERT INTO timesheets(emp,project,hours,approved,date) VALUES('"+emp+"','"+project+"',"+hoursS+","+(approvedS=="1"||approvedS=="true"?"1":"0")+",'"+date+"');";
                    sqlite3_exec(DB, sql.c_str(), nullptr, nullptr, nullptr);
                } else
#endif
                {
                    double h = atof(hoursS.c_str());
                    bool ap = (approvedS=="1"||approvedS=="true");
                    MEM_TS.push_back({emp,project,h,ap,date});
                }
                resp = http_created();
            }
        }
        else if(is_post && req.find("POST /timesheets/approve")==0){
            std::string b = body_of(req);
            std::string emp = json_get_string(b,"emp");
            std::string project = json_get_string(b,"project");
            std::string date = json_get_string(b,"date");
            if(emp.empty()||project.empty()){ resp = http_bad("emp/project required"); }
            else { bool ok = approve_timesheet_row(emp, project, date); resp = http_ok(std::string("{\"approved\":")+(ok?"true":"false")+"}"); }
        }
        else if(is_post && req.find("POST /expenses")==0){
            std::string b = body_of(req);
            std::string cat = json_get_string(b,"category");
            std::string amt = json_get_string(b,"amount");
            if(cat.empty()||amt.empty()){ resp = http_bad("invalid expense"); }
            else {
#ifdef FWO_WITH_SQLITE
                if(DB){
                    std::string sql = "INSERT INTO expenses(category,amount) VALUES('"+cat+"',"+amt+");";
                    sqlite3_exec(DB, sql.c_str(), nullptr, nullptr, nullptr);
                } else
#endif
                { MEM_EXP.push_back({cat, atof(amt.c_str())}); }
                resp = http_created();
            }
        }
        else if(is_post && req.find("POST /budget")==0){
            std::string b = body_of(req);
            std::string category = json_get_string(b,"category");
            std::string amount = json_get_string(b,"amount");
            if(category.empty()||amount.empty()){ resp = http_bad("category/amount required"); }
            else {
                double val = atof(amount.c_str());
                bool found=false;
                for(auto& bd: MEM_BUD){ if(bd.category==category){ bd.amount=val; found=true; break; } }
                if(!found) MEM_BUD.push_back({category, val});
                resp = http_created();
            }
        }
        else if(is_get && req.find("GET /budget/variance")==0){
            std::string out="["; bool first=true;
            for(const auto& bd: MEM_BUD){
                double actual = sum_expense(bd.category);
                double variance = bd.amount - actual;
                if(!first) out += ","; first=false;
                out += "{\"category\":\""+bd.category+"\",\"budget\":"+std::to_string(bd.amount)+",\"actual\":"+std::to_string(actual)+",\"variance\":"+std::to_string(variance)+"}";
            }
            out += "]"; resp = http_ok(out);
        }
        else if(is_get && req.find("GET /payroll")==0){
            std::string emp = url_param(req, "id");
            if(emp.empty()){ resp = http_bad("id required"); }
            else {
                double hours=0.0;
#ifdef FWO_WITH_SQLITE
                if(DB){
                    auto cb = [](void* ctx, int argc, char** argv, char** col)->int{
                        double* H = reinterpret_cast<double*>(ctx);
                        double h = argv[0]?atof(argv[0]):0.0;
                        int approved = argv[1]?atoi(argv[1]):0;
                        if(approved) *H += h;
                        return 0;
                    };
                    std::string sql = "SELECT hours,approved FROM timesheets WHERE emp='"+emp+"';";
                    sqlite3_exec(DB, sql.c_str(), cb, &hours, nullptr);
                } else
#endif
                {
                    for(const auto& t: MEM_TS) if(t.emp==emp && t.approved) hours += t.hours;
                }
                Payroll pr; auto pc = pr.compute(hours);
                std::ostringstream os;
                os << "{\"hours\":"<<hours<<",\"base\":"<<pc.base<<",\"overtime\":"<<pc.overtime<<",\"tax\":"<<pc.tax<<",\"net\":"<<pc.net<<"}";
                resp = http_ok(os.str());
            }
        }
        else if(is_get && req.find("GET /forecast/cashflow")==0){
            std::string winS = url_param(req, "window");
            int win = winS.empty()? 3 : std::max(1, std::min(12, atoi(winS.c_str())));
            int n = (int)MEM_EXP.size();
            double sum=0.0; int count=0;
            for(int i=n-1; i>=0 && count<win; --i){ sum += MEM_EXP[i].amount; count++; }
            double forecast = (count>0) ? sum / count : 0.0;
            std::ostringstream os; os << "{\"window\":"<<win<<",\"forecast\":"<<forecast<<"}";
            resp = http_ok(os.str());
        }
        else if(is_post && req.find("POST /invoice")==0){
            std::string b = body_of(req);
            std::string inv = json_get_string(b,"invoiceNo");
            std::string cust = json_get_string(b,"customerId");
            if(inv.empty()||cust.empty()){ resp = http_bad("invalid invoice"); }
            else {
                std::ofstream f(std::string("invoice_")+inv+".csv");
                f << "InvoiceNo,CustomerId,Description,Qty,UnitPrice,LineTotal\n";
                for(int i=0;i<2;i++){
                    std::string desc = json_get_string(b, i==0? "desc":"desc2");
                    std::string qty  = json_get_string(b, i==0? "qty":"qty2");
                    std::string price= json_get_string(b, i==0? "price":"price2");
                    if(desc.empty()) continue;
                    double q = atof(qty.c_str()); double p = atof(price.c_str());
                    f << inv << "," << cust << "," << desc << "," << q << "," << p << "," << (q*p) << "\n";
                }
                resp = http_created();
            }
        }
        else if(is_post && req.find("POST /ar/export")==0){
            std::string b = body_of(req);
            std::string cust = json_get_string(b,"customerId");
            std::string inv  = json_get_string(b,"invoiceNo");
            std::string total= json_get_string(b,"total");
            std::string tax  = json_get_string(b,"tax");
            if(cust.empty()||inv.empty()||total.empty()){ resp = http_bad("invalid ar"); }
            else {
                std::ofstream f("ar_export.csv");
                f << "CustomerId,InvoiceNo,Total,Tax\n";
                f << cust << "," << inv << "," << total << "," << (tax.empty()?"0":tax) << "\n";
                resp = http_created();
            }
        }
        else if(is_post && req.find("POST /ap/export")==0){
            std::string b = body_of(req);
            std::string po = json_get_string(b,"poNumber");
            std::string sup= json_get_string(b,"supplierId");
            std::string amt= json_get_string(b,"amount");
            std::string due= json_get_string(b,"dueDate");
            if(po.empty()||sup.empty()||amt.empty()||due.empty()){ resp = http_bad("invalid ap"); }
            else {
                std::ofstream f("ap_export.csv");
                f << "PONumber,SupplierId,Amount,DueDate\n";
                f << po << "," << sup << "," << amt << "," << due << "\n";
                resp = http_created();
            }
        }
        else if(is_post && req.find("POST /gl/export")==0){
            std::string b = body_of(req);
            std::string account = json_get_string(b,"account");
            std::string cc = json_get_string(b,"costCenter");
            std::string amt = json_get_string(b,"amount");
            std::string desc = json_get_string(b,"desc");
            if(account.empty()||cc.empty()||amt.empty()){ resp = http_bad("account/costCenter/amount required"); }
            else {
                std::ofstream f("gl_export.csv", std::ios::app);
                bool empty = f.tellp() == 0;
                if(empty) f << "Account,CostCenter,Amount,Description\n";
                f << account << "," << cc << "," << amt << "," << desc << "\n";
                resp = http_created();
            }
        }
        else if(is_get && req.find("GET /crm/customers")==0){
            std::string body="["; bool first=true;
            for(const auto& c: MEM_CUST){
                if(!first) body += ","; first=false;
                body += "{\"id\":\""+c.id+"\",\"name\":\""+c.name+"\",\"segment\":\""+c.segment+"\"}";
            }
            body += "]"; resp = http_ok(body);
        }
        else if(is_post && req.find("POST /crm/customers")==0){
            std::string b = body_of(req);
            std::string id = json_get_string(b,"id");
            std::string name = json_get_string(b,"name");
            std::string segment = json_get_string(b,"segment");
            if(id.empty()||name.empty()){ resp = http_bad("id/name required"); }
            else {
                bool updated=false;
                for(auto& c: MEM_CUST){ if(c.id==id){ c.name=name; c.segment=segment; updated=true; break; } }
                if(!updated) MEM_CUST.push_back({id,name,segment});
                resp = http_created();
            }
        }
        else {
            resp = http_notfound();
        }

        send(sock, resp.c_str(), resp.size(), 0);
        close(sock);
    }
    return 0;
}
