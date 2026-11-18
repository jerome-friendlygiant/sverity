// sverity-args: --top top
// expect: LNT-060 WARNING
// expect-confidence: LNT-060 established
module top(input logic clk, output logic y);
  logic q;
  task automatic inner(output logic v); logic tmp; tmp = v; endtask
  task automatic put(output logic v); inner(v); endtask
  always @(posedge clk) put(q);
  assign y = q;
endmodule
