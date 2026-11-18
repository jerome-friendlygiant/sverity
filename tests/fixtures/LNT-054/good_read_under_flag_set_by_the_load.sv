// sverity-args: --top top
// Both registers have no reset, and both are read only under a reset flag that is set by the event that loads them on the same edge:
// the flag is set under `ack` while `word_q` loads under `ack`, and copies `go` while `row_q` loads under `go`.
// expect-none: LNT-054
module top(input logic clk, rst_n, ack, go, input logic [3:0] d, input logic [3:0] e, output logic y, z);
  logic [3:0] word_q, row_q;
  logic seen_q, live_q;
  always_ff @(posedge clk) if (ack) word_q <= d;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) seen_q <= 1'b0;
    else if (ack) seen_q <= 1'b1;
  end
  assign y = (seen_q && ack && word_q == d) ? 1'b1 : 1'b0;
  always_ff @(posedge clk) if (go) row_q <= e;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) live_q <= 1'b0;
    else live_q <= go;
  end
  always_comb begin
    z = 1'b0;
    if (live_q) begin
      if (row_q[0]) z = 1'b1;
    end
  end
endmodule
