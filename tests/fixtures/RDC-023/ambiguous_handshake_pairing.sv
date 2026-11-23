// sverity-args: --goal rdc --enable-rule CDC-015 --enable-rule CDC-024 --top top --sdc @FIXTURES@/_common/async.sdc
// Two requests share one request synchronizer and one acknowledge; endpoint ownership is unresolved.
// The reset and protocol obligations are independent. Explicitly enabling CDC-015/024 keeps both visible in an RDC-scoped run.
// expect: CDC-024 WARNING
// expect-unresolved: CDC-024
// expect: RDC-023 WARNING
// expect-unresolved: RDC-023
module top(input logic clka, clkb, rst_n, start0, start1, output logic busy);
  logic req0, req1, req_meta, req_seen, ack, ack_meta, ack_seen;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin req0 <= 1'b0; req1 <= 1'b0; end
    else begin
      if (start0 && !req0 && !ack_seen) req0 <= 1'b1;
      else if (ack_seen) req0 <= 1'b0;
      if (start1 && !req1 && !ack_seen) req1 <= 1'b1;
      else if (ack_seen) req1 <= 1'b0;
    end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin req_meta <= 1'b0; req_seen <= 1'b0; end
    else begin req_meta <= req0 | req1; req_seen <= req_meta; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) ack <= 1'b0;
    else ack <= req_seen;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin ack_meta <= 1'b0; ack_seen <= 1'b0; end
    else begin ack_meta <= ack; ack_seen <= ack_meta; end
  assign busy = req0 | req1;
endmodule
