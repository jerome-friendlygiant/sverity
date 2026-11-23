// sverity-args: --goal rdc --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-015.reach_budget=1
// One candidate leg is reached before the reachability budget runs out; RDC-023 must remain Unresolved.
// expect: RDC-023 WARNING
// expect-unresolved: RDC-023
// expect-coverage: cdc-015-handshake-reachability INCOMPLETE
module top(input logic clka, clkb, rst_n, start, output logic req_out);
  logic req, req_meta, req_seen, ack, ack_meta, ack_seen;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) req <= 1'b0;
    else if (start && !ack_seen) req <= ~req;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin req_meta <= 1'b0; req_seen <= 1'b0; end
    else begin req_meta <= req; req_seen <= req_meta; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) ack <= 1'b0;
    else ack <= req_seen;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin ack_meta <= 1'b0; ack_seen <= 1'b0; end
    else begin ack_meta <= ack; ack_seen <= ack_meta; end
  assign req_out = req;
endmodule
