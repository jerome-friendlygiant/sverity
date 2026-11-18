// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk, output logic y);
  logic q;
  task automatic inspect(ref logic v); logic tmp; tmp = v; endtask
  always @(posedge clk) inspect(q);
  assign y = q;
endmodule
