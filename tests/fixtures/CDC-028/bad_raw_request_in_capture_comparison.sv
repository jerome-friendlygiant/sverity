// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-028 ERROR
// expect-confidence: CDC-028 established
// revert: req_q != sync_delayed => sync_q != sync_delayed
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, request, din, ready, output logic q);
  logic req_q, data_q, first, sync_q, sync_delayed;
  wire load = (req_q != sync_delayed) && ready;
  always_ff @(posedge clka) begin
    req_q <= request;
    if (request) data_q <= din;
  end
  always_ff @(posedge clkb) begin
    first <= req_q;
    sync_q <= first;
    sync_delayed <= sync_q;
    if (load) q <= data_q;
  end
endmodule
