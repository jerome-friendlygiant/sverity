// C07
module c07(input logic clk_s, clk_d, input logic [3:0] payload_s, input logic flag_s, output logic [3:0] payload_d, output logic flag_d);
  logic f1,f2;
  always_ff @(posedge clk_d) begin f1<=flag_s; f2<=f1; payload_d<=payload_s; end
  assign flag_d=f2;
endmodule
