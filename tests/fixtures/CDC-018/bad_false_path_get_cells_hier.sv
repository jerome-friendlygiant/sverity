// sverity-args: --top top --sdc @DIR@/false_path_get_cells_hier.sdc
// Hierarchical [get_cells u_src] / [get_cells u_dst] false path overlaps the Gray pointer crossing.
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-018 INFO
module src_mod(input logic clka, rst_n, inc, output logic [3:0] gray);
  logic [3:0] bin;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
endmodule
module dst_mod(input logic clkb, rst_n, input logic [3:0] gray, output logic [3:0] q);
  logic [3:0] s1, s2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] gray;
  src_mod u_src(.clka(clka), .rst_n(rst_n), .inc(inc), .gray(gray));
  dst_mod u_dst(.clkb(clkb), .rst_n(rst_n), .gray(gray), .q(q));
endmodule
