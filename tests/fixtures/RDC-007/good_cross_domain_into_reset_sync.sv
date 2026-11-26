// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A cross-domain reset feeding only stages of a recognised reset synchroniser in the receiving domain is exempt from RDC-007.
// expect-none: RDC-007
module top(input logic clka, clkb, por_n, d, output logic q);
  logic rst_a_q;
  logic [1:0] sync_b;
  always_ff @(posedge clka or negedge por_n)
    if (!por_n) rst_a_q <= 1'b0;
    else rst_a_q <= 1'b1;
  always_ff @(posedge clkb or negedge rst_a_q)
    if (!rst_a_q) sync_b <= 2'b00;
    else sync_b <= {sync_b[0], 1'b1};
  always_ff @(posedge clkb or negedge sync_b[1])
    if (!sync_b[1]) q <= 1'b0;
    else q <= d;
endmodule
