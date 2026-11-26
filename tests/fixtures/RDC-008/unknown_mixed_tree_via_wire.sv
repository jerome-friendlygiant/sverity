// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A wire hides an OR inside an AND: the whole tree mixes two operators, so it is not one classified form. Its assertion condition is Unresolved; it is never classified by the edges entering r alone.
// expect-unresolved: RDC-008
module top(input logic clka, rst_n, por_rst_n, sw_rst_n, d, output logic q);
  logic r;
  wire mid = rst_n | por_rst_n;
  assign r = mid & sw_rst_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
