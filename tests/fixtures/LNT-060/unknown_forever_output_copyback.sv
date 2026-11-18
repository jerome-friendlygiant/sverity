// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk, output logic y);
  logic q;
  task automatic put(output logic v); forever begin end endtask
  always @(posedge clk) put(q);
  assign y = q;
endmodule
