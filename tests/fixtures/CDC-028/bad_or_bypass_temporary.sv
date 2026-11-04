// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// expect: CDC-028 ERROR
// expect-none: CDC-039
// revert: synced | force_capture => synced
// revert-expect-none: CDC-003 ERROR
// revert-expect-none: CDC-028 ERROR
module top(input logic clka, clkb, rst_n, send, d, force_capture, ready, output logic q);
  logic payload, request, first, synced;
  wire capture = (synced | force_capture) && ready;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin payload <= 0; request <= 0; end
    else begin request <= send; if (send) payload <= d; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin first <= 0; synced <= 0; q <= 0; end
    else begin first <= request; synced <= first; if (capture) q <= payload; end
endmodule
