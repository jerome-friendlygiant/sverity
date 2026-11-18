// sverity-args: --top top
// A virtual method call may dispatch to an override that disables the caller's block: the loop is not known to run completely.
// expect: LNT-041 WARNING 5
module top(input logic c, output logic y);
  logic a [0:7];
  class Base; virtual task automatic step(input int v); endtask endclass
  Base h;
  always @(c) begin : blk
    for (int i = 0; i < 8; i++) begin h.step(i); a[i] = c; end
  end
  assign y = a[0];
endmodule
