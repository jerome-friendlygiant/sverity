// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// a design with a reset: no power intent (UPF/CPF) is supported or supplied, so retention/isolation resets are unknown, never safe
// expect: RDC-012 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
