// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of obligation_input_event_stream.sv: start is the synchronised bit itself, not an edge detect, and the chain is three stages deep (below the depth-4 trigger).
// expect-none: CDC-044
module top(input logic clka, rst_n, rx, output logic start);
  logic s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {s3, s2, s1} <= '0; else {s3, s2, s1} <= {s2, s1, rx};
  assign start = s3;
endmodule
