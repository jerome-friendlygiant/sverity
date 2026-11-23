// C11
module c11(input logic clk, arst_n, input logic d, output logic q);
  always_ff @(posedge clk or negedge arst_n) if (!arst_n) q<=0; else q<=d;
endmodule
