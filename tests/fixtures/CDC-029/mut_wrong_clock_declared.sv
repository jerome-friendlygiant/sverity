// sverity-args: --top top --sdc @DIR@/inputs.sdc
// MUTATION of good_declared_synchronous.sv: the input is declared against a clock asynchronous to the capture clock.
// expect: CDC-029 WARNING
module top(input logic clka, rst_n, dwrong, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= dwrong;
endmodule
