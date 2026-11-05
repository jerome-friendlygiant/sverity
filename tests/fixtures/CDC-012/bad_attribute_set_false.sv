// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile fpga
// ASYNC_REG = "FALSE" asks for the opposite of a synchroniser's placement, so it does not count as marked.
// expect: CDC-012 WARNING
module top(input logic clka, clkb, d, output logic q);
  logic launch;
  (* ASYNC_REG = "FALSE" *) logic s1;
  (* ASYNC_REG = "FALSE" *) logic s2;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) begin s1 <= launch; s2 <= s1; end
  assign q = s2;
endmodule
