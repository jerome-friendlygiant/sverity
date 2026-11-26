// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A first-stage CDC data synchroniser destination is exempt from RDC-005 when every consuming register of the synchroniser
// is itself reset whenever the source reset asserts (here via a synchronised copy ra1_q -> ra_b_q of rst_a).
// expect-none: RDC-005
module top(input logic clka, clkb, rst_a, rst_b, d, output logic q);
  logic src_q, sync1_q, sync2_q, ra1_q, ra_b_q, dst_q;
  always_ff @(posedge clka or negedge rst_a)
    if (!rst_a) src_q <= 1'b0;
    else src_q <= d;
  always_ff @(posedge clkb or negedge rst_b)
    if (!rst_b) begin
      sync1_q <= 1'b0; sync2_q <= 1'b0;
      ra1_q <= 1'b0; ra_b_q <= 1'b0;
    end else begin
      sync1_q <= src_q; sync2_q <= sync1_q;
      ra1_q <= rst_a; ra_b_q <= ra1_q;
    end
  always_ff @(posedge clkb or negedge rst_b)
    if (!rst_b || !ra_b_q) dst_q <= 1'b0;
    else dst_q <= sync2_q;
  assign q = dst_q;
endmodule
