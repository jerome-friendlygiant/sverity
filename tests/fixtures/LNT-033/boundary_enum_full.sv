// sverity-args: --top top
// expect-none: LNT-033
typedef enum bit [1:0] {IDLE, RUN, DONE, ERR} st_t;
module top(input st_t s, output logic y);
  always_comb begin
    y = 1'b0;
    unique case (s)
      IDLE: y = 1'b1;
      RUN: y = 1'b0;
      DONE: y = 1'b1;
      ERR: y = 1'b0;
    endcase
  end
endmodule
