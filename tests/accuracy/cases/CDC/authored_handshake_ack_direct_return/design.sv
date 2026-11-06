// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic out_o);
 logic req_q=0,req_m=0,req_s=0,ack_q=0;
 always_ff @(posedge sclk) begin
  if(src_i && !ack_q) req_q<=1;
  else if(ack_q) req_q<=0;
 end
 always_ff @(posedge dclk) begin
  req_m<=req_q;req_s<=req_m;
  ack_q<=req_s;
  out_o<=req_s;
 end
endmodule
