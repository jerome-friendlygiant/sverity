// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic signed [7:0] src_i, output logic out_o);
 logic signed [7:0] threshold_q=0;
 logic signed [7:0] sample_q=0;
 always_ff @(posedge sclk) threshold_q<=src_i;
 always_ff @(posedge dclk) begin
  sample_q<=sample_q+1'sb1;
  out_o<=(sample_q > threshold_q);
 end
endmodule
