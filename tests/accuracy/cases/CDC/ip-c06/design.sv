// C06
module c06(input logic clk_s, clk_d, input logic pulse_s, output logic pulse_d);
  logic p1,p2;
  always_ff @(posedge clk_d) begin p1<=pulse_s; p2<=p1; end
  assign pulse_d=p2;
endmodule
