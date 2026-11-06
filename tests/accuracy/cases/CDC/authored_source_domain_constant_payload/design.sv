// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk,input logic src_i,output logic [7:0] out_o);
 localparam logic [7:0] CHIP_ID=8'hA7;
 logic request_q=0,r1=0,r2=0;
 always_ff @(posedge sclk) request_q<=request_q|src_i;
 always_ff @(posedge dclk) begin
  r1<=request_q;r2<=r1;
  if(r2) out_o<=CHIP_ID;
 end
endmodule
