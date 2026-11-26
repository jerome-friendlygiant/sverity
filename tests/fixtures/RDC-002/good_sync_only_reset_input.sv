// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A top-level reset input bit whose every async-reset use is a stage of a recognised reset synchroniser is exempt from RDC-002.
// expect-none: RDC-002
module top(input logic clka, clkb, por_n, d_a, d_b, output logic q_a, q_b);
  logic [1:0] sync_a, sync_b;
  wire por_hi = ~por_n;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) sync_a <= 2'b00;
    else sync_a <= {sync_a[0], 1'b1};
  always_ff @(posedge clkb or posedge por_hi)
    if (por_hi) sync_b <= 2'b11;
    else sync_b <= {sync_b[0], 1'b0};
  always_ff @(posedge clka or negedge sync_a[1])
    if (!sync_a[1]) q_a <= 1'b0;
    else q_a <= d_a;
  always_ff @(posedge clkb or posedge sync_b[1])
    if (sync_b[1]) q_b <= 1'b0;
    else q_b <= d_b;
endmodule
