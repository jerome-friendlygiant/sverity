// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: ev <= s2 | s3; => ev <= s2 ^ s3;
// revert-expect-none: CDC-014 WARNING
// OR of the two copies stays high while the toggle is high: a level, not an event, so no edge detector is recognised.
// expect: CDC-014 WARNING
module top(input logic clka, clkb, rst_n, pulse, output logic ev);
  logic tgl, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) tgl <= 1'b0; else if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; s3 <= 1'b0; ev <= 1'b0; end
    else begin s1 <= tgl; s2 <= s1; s3 <= s2; ev <= s2 | s3; end
endmodule
