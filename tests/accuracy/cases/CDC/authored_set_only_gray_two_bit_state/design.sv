// Authored for the SVerity accuracy set.
// State advances only once at each stage and never returns or cycles.
module top(input logic sclk,dclk,input logic [1:0] src_i,output logic [1:0] out_o);
 logic started_q=0,finished_q=0;
 logic a1=0,a2=0,b1=0,b2=0;
 always_ff @(posedge sclk) begin
  started_q<=started_q|src_i[0];
  finished_q<=finished_q|(started_q && src_i[1]);
 end
 always_ff @(posedge dclk) begin
  a1<=started_q;a2<=a1;
  b1<=finished_q;b2<=b1;
  out_o<={b2,a2};
 end
endmodule
