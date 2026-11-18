// sverity-args: --top top
// `w` is only wired to an instance of a module that is not defined: that module's output may drive it, so it is not established that
// nothing does (LNT-002 reports the missing module).
// expect-unresolved: LNT-008 6
module top(input logic clk, output logic y);
  logic w;
  missing_cell u (.clk(clk), .out(w));
  always_ff @(posedge clk) y <= w;
endmodule
