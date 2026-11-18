// sverity-args: --top top
// expect: LNT-066 WARNING 14
// expect-count: LNT-066 WARNING 1
module top(input bit safe_index, output logic y, safe);
  logic poisoned_index;
  assign poisoned_index = 1'bx;
  function automatic logic make_mask(input logic index);
    logic [1:0] mask;
    mask = 0;
    mask[index] = 1;
    return |mask;
  endfunction
  assign safe = make_mask(safe_index);
  assign y = make_mask(poisoned_index);
endmodule
