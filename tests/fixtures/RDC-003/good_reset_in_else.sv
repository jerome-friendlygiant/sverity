// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
// expect-none: RDC-006
module top(input logic clka, rst_n, d, output logic q);
  logic first, middle, last;
  always_ff @(posedge clka or negedge rst_n)
    if (rst_n) begin first <= 1; middle <= first; last <= middle; end
    else begin first <= 0; middle <= 0; last <= 0; end
  always_ff @(posedge clka or negedge last) if (!last) q <= 0; else q <= d;
endmodule
