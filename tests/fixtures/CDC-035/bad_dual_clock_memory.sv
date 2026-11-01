// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb) rd <= mem[ra]; => always_ff @(posedge clka) rd <= mem[ra];
// revert-expect-none: CDC-035 WARNING
// expect: CDC-035 WARNING
module top(input logic clka, clkb, rst_n, we, input logic [1:0] wa, ra, input logic [3:0] wd, output logic [3:0] rd);
  logic [3:0] mem [4];
  always_ff @(posedge clka) if (we) mem[wa] <= wd;
  always_ff @(posedge clkb) rd <= mem[ra];
endmodule
