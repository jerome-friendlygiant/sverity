// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// The payload is captured under a valid synchronised through two stages where three are required: the qualifier may itself be
// metastable when it opens the capture, so qualification stays unresolved rather than recognised.
// expect: CDC-039 WARNING
// expect-none: CDC-039 INFO
// expect: CDC-005 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module top(input logic clka, clkb, rst_n, v, input logic [7:0] d, output logic [7:0] q);
  logic v_a, s1, s2;
  logic [7:0] d_a;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin v_a <= 1'b0; d_a <= '0; end else begin v_a <= v; d_a <= d; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, v_a};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= '0; else if (s2) q <= d_a;
endmodule
