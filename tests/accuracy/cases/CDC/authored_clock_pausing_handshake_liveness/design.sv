// Authored for the SVerity accuracy set.
// The board may stop dclk; no restart or service-time guarantee is provided.
module top(input logic sclk,dclk,input logic src_i,output logic out_o);
 logic req_q=0,ack_q=0,a1=0,a2=0,r1=0,r2=0;
 always_ff @(posedge sclk) begin
  a1<=ack_q;a2<=a1;
  if(src_i && !req_q && !a2) req_q<=1;
  if(req_q && a2) req_q<=0;
 end
 always_ff @(posedge dclk) begin
  r1<=req_q;r2<=r1;
  ack_q<=r2;out_o<=r2;
 end
endmodule
