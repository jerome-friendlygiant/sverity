// C10
module c10(input logic clk, en, d, output logic q);
  wire gclk=clk & en;
  always_ff @(posedge gclk) q<=d;
endmodule
