// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk, en, output logic y);
  logic q;
  task automatic put(output logic v); fork wait(en); join endtask
  always @(posedge clk) put(q);
  assign y = q;
endmodule
