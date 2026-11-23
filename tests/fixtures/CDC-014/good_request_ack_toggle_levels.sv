// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The request toggle is consumed as a level until the acknowledgement returns.
// expect: CDC-015 INFO
// expect-none: CDC-014 WARNING
module top(input logic clka, clkb, start_i, output logic done_o);
  logic req_q, ack_q, a1, a2, r1, r2;
  always_ff @(posedge clka) begin
    a1 <= ack_q;
    a2 <= a1;
    if (start_i && req_q == a2) req_q <= ~req_q;
  end
  always_ff @(posedge clkb) begin
    r1 <= req_q;
    r2 <= r1;
    if (r2 != ack_q) ack_q <= r2;
  end
  assign done_o = ack_q;
endmodule
