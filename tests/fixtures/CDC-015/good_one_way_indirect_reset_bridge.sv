// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An async-reset constant-release bridge indirectly reaching an opposite crossing is not a broken handshake.
// expect-none: CDC-015 WARNING
module top(input logic clka, clkb, rst_n, req_en, output logic busy);
  logic bridge_q, br_s1, br_s2, mid_q, req_q, rq_s1, rq_s2;
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) bridge_q <= 1'b1;
    else bridge_q <= 1'b0;
  end
  always_ff @(posedge clka) begin
    br_s1 <= bridge_q;
    br_s2 <= br_s1;
    mid_q <= br_s2 & req_en;
    req_q <= mid_q;
  end
  always_ff @(posedge clkb) begin
    rq_s1 <= req_q;
    rq_s2 <= rq_s1;
  end
  assign busy = rq_s2;
endmodule
