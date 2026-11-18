// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic sel;
  logic [1:0] data;
  always @(posedge clk) sel = 0;
  assign y = data[sel];
endmodule
