// sverity-args: --top top
// The first process stops at k == 4 through a task's disable, so it writes v[3:0] only; the second writes v[7:4]. slang's
// unrolled drivers name every k, but the bits written are not known: no certain conflict.
// expect: LNT-009 WARNING 11
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  task automatic stop(input int k); if (k == 4) disable blk; endtask
  always @(c) begin : blk
    for (int k = 0; k < 8; k++) begin stop(k); v[k] = c; end
  end
  always @(c) v[7:4] = 4'h0;
  assign y = v;
endmodule
