// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Each control forces its value on its own branch; the enable d is evaluated only after both controls are inactive, so both
// clear and set are established as a conflict on q.
// expect: RDC-026 WARNING
// expect-confidence: RDC-026 established
module top(input logic clka, rst_n, por_rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n or negedge por_rst_n) if (!rst_n) q <= 0; else if (!por_rst_n) q <= 1; else if (d) q <= 1;
endmodule
