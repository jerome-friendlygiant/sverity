// Authored for the SVerity accuracy set.
// Each output bit independently means that its corresponding alarm has occurred.
module top(input logic sclk,dclk,input logic [2:0] src_i,output logic [2:0] out_o);
 typedef struct packed {logic thermal,voltage,watchdog;} alarms_t;
 alarms_t alarm_q='0;
 logic [2:0] meta_q=0,safe_q=0;
 always_ff @(posedge sclk) alarm_q<=alarm_q | src_i;
 always_ff @(posedge dclk) begin
  meta_q<=alarm_q;safe_q<=meta_q;
  out_o<=safe_q;
 end
endmodule
