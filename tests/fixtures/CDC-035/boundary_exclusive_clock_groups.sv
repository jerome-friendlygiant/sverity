// sverity-args: --top top --sdc @DIR@/exclusive.sdc
// An unpacked memory crossing exclusive domains is reported informationally.
// expect: CDC-035 INFO
module top(input logic clka, clkb, we, input logic [1:0] wa, ra, input logic [3:0] wd, output logic [3:0] rd);
  logic [3:0] mem [4];
  always_ff @(posedge clka) if (we) mem[wa] <= wd;
  always_ff @(posedge clkb) rd <= mem[ra];
endmodule
