// Authored for the SVerity accuracy set.
// The host increments between observation windows; the window spacing is unspecified.
module top(input logic sclk,dclk,input logic [1:0] src_i,output logic [7:0] out_o);
 logic [7:0] count_q=0,shadow_q=0;
 logic valid_q=0,v1=0,v2=0;
 always_ff @(posedge sclk) begin
  if(src_i[0]) count_q<=count_q+1'b1;
  if(src_i[1]) begin shadow_q<=count_q;valid_q<=~valid_q;end
 end
 logic seen_q=0;
 always_ff @(posedge dclk) begin
  v1<=valid_q;v2<=v1;seen_q<=v2;
  if(v2!=seen_q) out_o<=shadow_q;
 end
endmodule
