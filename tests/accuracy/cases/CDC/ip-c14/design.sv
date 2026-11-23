// C14
module c14(input logic clk_d, async_in, output logic q);
  always_ff @(posedge clk_d) q<=async_in;
endmodule
