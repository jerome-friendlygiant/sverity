// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_qualified_payload.sv: the capture enable comes from the destination domain (not synchronisation).
// CDC-004 is evidence of the primary CDC-028 finding
// expect: CDC-028 ERROR
// expect-none: CDC-039
module top(input logic clka, clkb, rst_n, load, en_b, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] data;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) data <= '0; else if (load) data <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= '0; else if (en_b) q <= data;
endmodule
