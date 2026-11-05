// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Near miss of bad_generated_clock.sv: the divided register and its create_generated_clock are gone; q is clocked by the primary clock clka. CDC-030 is an obligation per declared generated clock, so the SDC declares none (async.sdc, not the bad file's gen.sdc, which would leave a dangling create_generated_clock).
// expect-none: CDC-030
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
