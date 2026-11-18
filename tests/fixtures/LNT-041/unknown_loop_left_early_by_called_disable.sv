// sverity-args: --top top
// A called task disables the block at i == 4: elements 4 to 7 are never written, and the loop cannot be counted as complete.
// expect: LNT-041 WARNING 5
module top(input logic c, output logic y);
  logic a [8];
  task automatic stop(input int i); if (i == 4) disable blk; endtask
  always @(c) begin : blk
    for (int i = 0; i < 8; i++) begin stop(i); a[i] = c; end
  end
  assign y = a[7];
endmodule
