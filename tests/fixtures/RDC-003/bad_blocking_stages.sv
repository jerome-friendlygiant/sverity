// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: ALL  =  =>  <=
// revert-expect-none: RDC-003 WARNING
// expect: RDC-003 WARNING
// expect-none: RDC-003 INFO
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  logic first, last;
  always @(posedge clka or negedge rst_n)
    if (!rst_n) begin first = 0; last = 0; end
    else begin first = 1; last = first; end
  always_ff @(posedge clka or negedge last)
    if (!last) q = 0; else q = d;
endmodule
